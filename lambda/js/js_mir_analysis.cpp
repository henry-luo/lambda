#include "js_mir_internal.hpp"

uint64_t jm_name_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JsNameSetEntry* e = (const JsNameSetEntry*)item;
    return hashmap_sip(e->name, strlen(e->name), seed0, seed1);
}

int jm_name_cmp(const void* a, const void* b, void* udata) {
    return strcmp(((const JsNameSetEntry*)a)->name, ((const JsNameSetEntry*)b)->name);
}

bool jm_function_decl_is_direct_binding(JsFunctionNode* fn, bool arrow_body_is_direct) {
    (void)arrow_body_is_direct;
    if (!fn || !fn->vars || !fn->vars->parent) return false;
    NameScope* declaration_scope = fn->vars->parent;
    if (declaration_scope->kind == SCOPE_KIND_GLOBAL ||
            declaration_scope->kind == SCOPE_KIND_MODULE) {
        return true;
    }
    if (declaration_scope->kind != SCOPE_KIND_BLOCK || !declaration_scope->parent)
        return false;
    // The builder creates a block scope for a function body. A declaration in
    // that block is direct; a declaration in any deeper block is Annex B
    // block-scoped and must be recreated at runtime.
    return declaration_scope->parent->kind == SCOPE_KIND_FUNCTION;
}

// Forward declare
void jm_collect_pattern_names(JsAstNode* pat, struct hashmap* names);

void jm_name_set_add(struct hashmap* set, const char* name) {
    JsNameSetEntry e;
    memset(&e, 0, sizeof(e));
    e.name = jm_persist_name(name);
    // preserve existing var_kind if already in set
    JsNameSetEntry* existing = (JsNameSetEntry*)hashmap_get(set, &e);
    if (existing) return;  // already added
    hashmap_set(set, &e);
}

void jm_name_set_add_kind(struct hashmap* set, const char* name, int kind) {
    JsNameSetEntry e;
    memset(&e, 0, sizeof(e));
    e.name = jm_persist_name(name);
    e.var_kind = kind;
    hashmap_set(set, &e);
}

static bool jm_binding_is_inside_range(uint32_t binding_start, uint32_t binding_end,
                                       uint32_t body_start, uint32_t body_end) {
    return (binding_start != 0 || binding_end != 0) &&
        binding_start >= body_start && binding_end <= body_end;
}

// identify a lexical binding from the AST instead of copying its generated
// name into a fixed-capacity function-side table.
static bool jm_entry_is_lexical_for_head(NameEntry* entry) {
    return entry && entry->is_lexical && entry->is_for_in_head;
}

static void jm_name_set_add_ref(struct hashmap* set, const char* name, JsIdentifierNode* id,
                                uint32_t body_start, uint32_t body_end) {
    JsNameSetEntry e;
    memset(&e, 0, sizeof(e));
    e.name = jm_persist_name(name);
    if (id && id->entry && id->entry->node) {
        JsAstNode* def = (JsAstNode*)id->entry->node;
        e.binding_start = def->source_span.start_byte;
        e.binding_end = def->source_span.end_byte;
        e.var_kind = id->entry->is_const ? JS_VAR_CONST :
            (id->entry->is_lexical ? JS_VAR_LET : 0);
        e.entry = id->entry;
    }
    JsNameSetEntry* existing = (JsNameSetEntry*)hashmap_get(set, &e);
    if (existing) {
        bool existing_is_local = jm_binding_is_inside_range(
            existing->binding_start, existing->binding_end, body_start, body_end);
        bool candidate_is_local = jm_binding_is_inside_range(
            e.binding_start, e.binding_end, body_start, body_end);
        if (body_end > body_start && existing_is_local && !candidate_is_local) {
            // A name-keyed reference set must retain the free binding when a
            // nested lexical reuses its spelling; otherwise the lexical masks
            // the closure capture solely because it appears first in source.
            existing->binding_start = e.binding_start;
            existing->binding_end = e.binding_end;
            existing->var_kind = e.var_kind;
            existing->entry = e.entry;
            return;
        }
        if ((existing->binding_start == 0 && existing->binding_end == 0) &&
            (e.binding_start != 0 || e.binding_end != 0)) {
            existing->binding_start = e.binding_start;
            existing->binding_end = e.binding_end;
            existing->entry = e.entry;
        }
        if (!existing->entry && e.entry) existing->entry = e.entry;
        if (existing->var_kind == 0 && e.var_kind != 0) existing->var_kind = e.var_kind;
        return;
    }
    hashmap_set(set, &e);
}

static void jm_name_set_add_binding(struct hashmap* set, const char* name, JsAstNode* binding_node) {
    JsNameSetEntry e;
    memset(&e, 0, sizeof(e));
    e.name = jm_persist_name(name);
    if (binding_node) {
        e.binding_start = binding_node->source_span.start_byte;
        e.binding_end = binding_node->source_span.end_byte;
    }
    JsNameSetEntry* existing = (JsNameSetEntry*)hashmap_get(set, &e);
    if (existing) {
        if ((existing->binding_start == 0 && existing->binding_end == 0) &&
            (e.binding_start != 0 || e.binding_end != 0)) {
            existing->binding_start = e.binding_start;
            existing->binding_end = e.binding_end;
        }
        return;
    }
    hashmap_set(set, &e);
}

bool jm_name_set_has(struct hashmap* set, const char* name) {
    JsNameSetEntry key;
    memset(&key, 0, sizeof(key));
    key.name = jm_persist_name(name);
    return hashmap_get(set, &key) != NULL;
}

static bool jm_ref_is_local_binding(struct hashmap* locals, JsNameSetEntry* ref) {
    if (!locals || !ref) return false;
    JsNameSetEntry* local = (JsNameSetEntry*)hashmap_get(locals, ref);
    if (!local) return false;
    if (ref->binding_start == 0 && ref->binding_end == 0) return true;
    // A name-only local entry cannot shadow a reference already resolved to a
    // different binding; the function-range check handles true local bindings.
    if (local->binding_start == 0 && local->binding_end == 0) return false;
    return local->binding_start == ref->binding_start &&
        local->binding_end == ref->binding_end;
}

static bool jm_ref_binding_is_inside_function(JsFunctionNode* fn,
                                               JsNameSetEntry* ref) {
    if (!fn || !fn->body || !ref ||
        (ref->binding_start == 0 && ref->binding_end == 0)) {
        return false;
    }
    uint32_t body_start = fn->body->source_span.start_byte;
    uint32_t body_end = fn->body->source_span.end_byte;
    return ref->binding_start >= body_start && ref->binding_end <= body_end;
}

// Suspension facts are indexed once with the function owner, so nested
// closures cannot leak yield/await points into their enclosing state machine.
typedef enum JsSuspensionKind {
    JS_SUSPENSION_YIELD,
    JS_SUSPENSION_AWAIT,
} JsSuspensionKind;

static int jm_count_indexed_suspensions(JsMirTranspiler* mt, JsAstNode* root,
        JsSuspensionKind kind) {
    if (!mt || !mt->tp || !root) return 0;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId root_id = ast_index_find(index, (AstNode*)root);
    if (root_id == AST_NODE_ID_INVALID) return 0;
    AstFunctionId owner = index->owner_functions[root_id];
    int count = 0;
    for (uint32_t i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (!node || index->owner_functions[i] != owner ||
                !ast_index_node_descends(index, i, root_id)) continue;
        if (kind == JS_SUSPENSION_YIELD &&
                node->node_type == JS_AST_NODE_YIELD_EXPRESSION) {
            count++;
            // A yield in an array-pattern element resumes through both
            // iterator-result branches for every enclosing pattern level.
            AstNodeId parent_id = i;
            while (parent_id != root_id && parent_id != AST_NODE_ID_INVALID) {
                parent_id = ast_index_parent_id(index, parent_id);
                AstNode* parent = parent_id < index->count ? index->nodes[parent_id] : NULL;
                if (parent && parent->node_type == JS_AST_NODE_ARRAY_PATTERN) count++;
            }
        } else if (kind == JS_SUSPENSION_AWAIT &&
                node->node_type == JS_AST_NODE_AWAIT_EXPRESSION) {
            count++;
        }
        if (kind == JS_SUSPENSION_AWAIT &&
                node->node_type == JS_AST_NODE_FOR_OF_STATEMENT &&
                ((JsForOfNode*)node)->is_await) {
            count += 2;
        }
    }
    return count;
}

// Generator yield spill: save a temporary register to an env slot before a yield-containing
// sub-expression, so that its value survives the yield suspend/resume cycle.
// Returns the allocated env slot index.
int jm_gen_spill_save(JsMirTranspiler* mt, MIR_reg_t reg) {
    int slot = mt->gen_spill_slot_next++;
    jm_emit_store_i64(mt, slot * (int)sizeof(uint64_t), mt->gen_env_reg, reg);
    return slot;
}

// Generator yield spill: restore a register from an env slot after a yield-containing
// sub-expression has been evaluated.
void jm_gen_spill_load(JsMirTranspiler* mt, MIR_reg_t reg, int slot) {
    jm_emit_load_i64(mt, reg, slot * (int)sizeof(uint64_t), mt->gen_env_reg);
}

// Check if an expression subtree contains a yield (for generator spill decisions)
bool jm_has_yield(JsMirTranspiler* mt, JsAstNode* node) {
    return jm_count_yields(mt, node) > 0;
}

// Check if an expression subtree contains an optional chain (?.),
// meaning the result may be undefined due to short-circuiting.
bool jm_has_optional_chain(JsAstNode* node) {
    if (!node) return false;
    if (node->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* m = (JsMemberNode*)node;
        if (m->optional) return true;
        return jm_has_optional_chain(m->object);
    }
    if (node->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        JsCallNode* c = (JsCallNode*)node;
        if (c->optional) return true;
        return jm_has_optional_chain(c->callee);
    }
    return false;
}


int jm_count_yields(JsMirTranspiler* mt, JsAstNode* node) {
    return jm_count_indexed_suspensions(mt, node, JS_SUSPENSION_YIELD);
}
int jm_count_awaits(JsMirTranspiler* mt, JsAstNode* node) {
    return jm_count_indexed_suspensions(mt, node, JS_SUSPENSION_AWAIT);
}

// Assignment facts use the same indexed owner boundary as captures and
// suspensions. A `with` body is intentionally omitted because its writes are
// object-environment effects rather than lexical binding mutations.
void jm_collect_indexed_func_assignments(JsMirTranspiler* mt, JsAstNode* root,
        struct hashmap* names) {
    if (!mt || !mt->tp || !root || !names) return;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId root_id = ast_index_find(index, (AstNode*)root);
    if (root_id == AST_NODE_ID_INVALID) return;
    AstFunctionId owner = index->owner_functions[root_id];
    for (uint32_t i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (!node || index->owner_functions[i] != owner ||
                !ast_index_node_descends(index, i, root_id)) continue;
        bool in_with = false;
        AstNodeId parent_id = i;
        while (parent_id != root_id && parent_id != AST_NODE_ID_INVALID) {
            parent_id = ast_index_parent_id(index, parent_id);
            AstNode* parent = parent_id < index->count ? index->nodes[parent_id] : NULL;
            if (parent && parent->node_type == JS_AST_NODE_WITH_STATEMENT) {
                in_with = true;
                break;
            }
        }
        if (in_with) continue;
        JsIdentifierNode* id = NULL;
        if (node->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION) {
            JsAssignmentNode* assignment = (JsAssignmentNode*)node;
            if (assignment->left && assignment->left->node_type == JS_AST_NODE_IDENTIFIER) {
                id = (JsIdentifierNode*)assignment->left;
            }
        } else if (node->node_type == JS_AST_NODE_UNARY_EXPRESSION) {
            JsUnaryNode* unary = (JsUnaryNode*)node;
            if ((unary->op == JS_OP_INCREMENT || unary->op == JS_OP_DECREMENT) &&
                    unary->operand && unary->operand->node_type == JS_AST_NODE_IDENTIFIER) {
                id = (JsIdentifierNode*)unary->operand;
            }
        }
        if (id && id->name) jm_name_set_add(names, jm_var_name(id->name));
    }
}

// Collect local declarations from the indexed owner graph. Function bodies
// are excluded by owner id; direct function declarations are retained as the
// hoisted binding in their enclosing owner.
typedef struct JmIndexedBodyLocals {
    struct hashmap* locals;
    AstFunctionId owner;
    bool var_only;
} JmIndexedBodyLocals;

static bool jm_collect_indexed_body_local(const AstIndex* index, AstNodeId node_id,
        void* opaque) {
    JmIndexedBodyLocals* context = (JmIndexedBodyLocals*)opaque;
    AstNode* current = index->nodes[node_id];
    if (!current) return true;
    bool same_owner = index->owner_functions[node_id] == context->owner;
    AstNodeId parent_id = ast_index_parent_id(index, node_id);
    bool enclosing_owner = parent_id != AST_NODE_ID_INVALID &&
        index->owner_functions[parent_id] == context->owner;
    if (!same_owner && !(current->node_type == JS_AST_NODE_FUNCTION_DECLARATION &&
            enclosing_owner)) return true;
    if (current->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* declaration = (JsVariableDeclarationNode*)current;
        if (context->var_only && declaration->kind != JS_VAR_VAR) return true;
        for (JsAstNode* d = declaration->declarations; d; d = d->next) {
            if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)d;
                if (declarator->id) jm_collect_pattern_names(declarator->id, context->locals);
            }
        }
    } else if (current->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
        JsFunctionNode* fn = (JsFunctionNode*)current;
        if (context->var_only && (fn->is_generator || fn->is_async)) return true;
        if (fn->name) {
            JsNameSetEntry entry;
            memset(&entry, 0, sizeof(entry));
            entry.name = jm_persist_name(jm_var_name(fn->name));
            entry.from_func_decl = true;
            if (!hashmap_get(context->locals, &entry)) hashmap_set(context->locals, &entry);
        }
    } else if (!context->var_only && current->node_type == JS_AST_NODE_CLASS_DECLARATION) {
        JsClassNode* cls = (JsClassNode*)current;
        if (cls->name) jm_name_set_add(context->locals, jm_var_name(cls->name));
    } else if (current->node_type == JS_AST_NODE_FOR_OF_STATEMENT ||
            current->node_type == JS_AST_NODE_FOR_IN_STATEMENT) {
        JsForOfNode* loop = (JsForOfNode*)current;
        if (!loop->left) return true;
        if (loop->left->node_type == JS_AST_NODE_IDENTIFIER) {
            if (!context->var_only || loop->kind == JS_VAR_VAR) {
                if (loop->kind == JS_VAR_LET || loop->kind == JS_VAR_CONST) {
                    JsIdentifierNode* id = (JsIdentifierNode*)loop->left;
                    jm_name_set_add_binding(context->locals, jm_var_name(id->name), loop->left);
                } else {
                    jm_collect_pattern_names(loop->left, context->locals);
                }
            }
        } else if (loop->left->node_type != JS_AST_NODE_VARIABLE_DECLARATION &&
                loop->declares_binding && (!context->var_only || loop->kind == JS_VAR_VAR)) {
            jm_collect_pattern_names(loop->left, context->locals);
        }
    }
    return true;
}

void jm_collect_indexed_body_locals(JsMirTranspiler* mt, JsAstNode* node,
        struct hashmap* locals, bool var_only) {
    if (!mt || !mt->tp || !node || !locals) return;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId root_id = ast_index_find(index, (AstNode*)node);
    if (root_id == AST_NODE_ID_INVALID) return;
    JmIndexedBodyLocals context = {locals, index->owner_functions[root_id], var_only};
    if (!ast_index_visit_subtree(index, root_id, jm_collect_indexed_body_local, &context)) {
        log_error("js-mir: unable to visit indexed body locals");
    }
}

// v20 TDZ: Collect names of let/const variables declared in a block statement.
// Only scans direct children (non-recursive into nested blocks/functions).
// Adds names with _js_ prefix to the set, with var_kind set to 1 (let) or 2 (const).
void jm_collect_let_const_names(JsAstNode* block, struct hashmap* names) {
    if (!block || block->node_type != JS_AST_NODE_BLOCK_STATEMENT) return;
    JsBlockNode* blk = (JsBlockNode*)block;
    JsAstNode* stmt = blk->statements;
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)stmt;
            if (v->kind == JS_VAR_LET || v->kind == JS_VAR_CONST) {
                JsAstNode* d = v->declarations;
                while (d) {
                    if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
                        if (decl->id) {
                            if (decl->id->node_type == JS_AST_NODE_IDENTIFIER) {
                                JsIdentifierNode* id = (JsIdentifierNode*)decl->id;
                                const char* name = jm_var_name(id->name);
                                JsNameSetEntry entry;
                                memset(&entry, 0, sizeof(entry));
                                entry.name = jm_persist_name(name);
                                entry.var_kind = (int)v->kind;
                                JsAstNode* binding_node = id->entry && id->entry->node ?
                                    (JsAstNode*)id->entry->node : decl->id;
                                if (binding_node) {
                                    entry.binding_start = binding_node->source_span.start_byte;
                                    entry.binding_end = binding_node->source_span.end_byte;
                                }
                                hashmap_set(names, &entry);
                            } else {
                                struct hashmap* pat_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                                    jm_name_hash, jm_name_cmp, NULL, NULL);
                                jm_collect_pattern_names(decl->id, pat_names);
                                size_t piter = 0;
                                void* pitem = NULL;
                                while (hashmap_iter(pat_names, &piter, &pitem)) {
                                    JsNameSetEntry* ne = (JsNameSetEntry*)pitem;
                                    jm_name_set_add_kind(names, ne->name, (int)v->kind);
                                }
                                hashmap_free(pat_names);
                            }
                        }
                    }
                    d = d->next;
                }
            }
        } else if (stmt->node_type == JS_AST_NODE_CLASS_DECLARATION) {
            JsClassNode* c = (JsClassNode*)stmt;
            if (c->name) {
                const char* name = jm_var_name(c->name);
                jm_name_set_add_kind(names, name, (int)JS_VAR_LET);
            }
        } else if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            JsFunctionNode* fn = (JsFunctionNode*)stmt;
            if (fn->name) {
                const char* name = jm_var_name(fn->name);
                jm_name_set_add_kind(names, name, (int)JS_VAR_LET);
                JsNameSetEntry lookup;
                memset(&lookup, 0, sizeof(lookup));
                lookup.name = jm_persist_name(name);
                JsNameSetEntry* e = (JsNameSetEntry*)hashmap_get(names, &lookup);
                if (e) e->from_func_decl = true;
            }
        }
        stmt = stmt->next;
    }
}

void jm_collect_switch_lexical_names(JsAstNode* switch_node, struct hashmap* names) {
    if (!switch_node || switch_node->node_type != JS_AST_NODE_SWITCH_STATEMENT || !names) return;
    JsSwitchNode* sw = (JsSwitchNode*)switch_node;
    for (JsAstNode* c = sw->cases; c; c = c->next) {
        if (c->node_type != JS_AST_NODE_SWITCH_CASE) continue;
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)c;
        // Switch cases use the same direct-lexical contract as a block.
        JsBlockNode block = {};
        block.node_type = JS_AST_NODE_BLOCK_STATEMENT;
        block.statements = sc->consequent;
        jm_collect_let_const_names((JsAstNode*)&block, names);
    }
}

// AnnexB lexical-name collection uses the shared AST child contract while
// retaining declaration and function/class boundary exceptions.
static void jm_collect_all_let_const_child(JsAstNode* child, void* opaque);

void jm_collect_all_let_const_names_recursive(JsAstNode* node, struct hashmap* names) {
    if (!node || !names) return;
    if (node->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* declaration = (JsVariableDeclarationNode*)node;
        if (declaration->kind == JS_VAR_LET || declaration->kind == JS_VAR_CONST) {
            for (JsAstNode* item = declaration->declarations; item; item = item->next) {
                if (item->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
                JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)item;
                if (declarator->id && declarator->id->node_type == JS_AST_NODE_IDENTIFIER) {
                    jm_name_set_add(names, jm_var_name(((JsIdentifierNode*)declarator->id)->name));
                }
            }
        }
    } else if (node->node_type == JS_AST_NODE_FOR_IN_STATEMENT ||
            node->node_type == JS_AST_NODE_FOR_OF_STATEMENT) {
        JsForInNode* loop = (JsForInNode*)node;
        if (loop->left && (loop->kind == JS_VAR_LET || loop->kind == JS_VAR_CONST) &&
                loop->left->node_type == JS_AST_NODE_IDENTIFIER) {
            jm_name_set_add(names, jm_var_name(((JsIdentifierNode*)loop->left)->name));
        }
    } else if (node->node_type == JS_AST_NODE_CLASS_DECLARATION) {
        JsClassNode* cls = (JsClassNode*)node;
        if (cls->name) jm_name_set_add(names, jm_var_name(cls->name));
        return;
    } else if (node->node_type == JS_AST_NODE_FUNCTION_DECLARATION ||
            node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
            node->node_type == JS_AST_NODE_ARROW_FUNCTION ||
            node->node_type == JS_AST_NODE_METHOD_DEFINITION ||
            node->node_type == JS_AST_NODE_CLASS_EXPRESSION) {
        return;
    } else if (node->node_type == JS_AST_NODE_CATCH_CLAUSE) {
        JsCatchNode* catch_node = (JsCatchNode*)node;
        if (catch_node->param && catch_node->param->node_type != JS_AST_NODE_IDENTIFIER) {
            jm_collect_pattern_names(catch_node->param, names);
        }
    }
    js_ast_visit_children(node, jm_collect_all_let_const_child, names);
}

static void jm_collect_all_let_const_child(JsAstNode* child, void* opaque) {
    jm_collect_all_let_const_names_recursive(child, (struct hashmap*)opaque);
}

// v20 TDZ: Initialize let/const variables in a block to TDZ sentinel.
// Call at block entry (after jm_push_scope) before transpiling block statements.
static JsMirVarEntry* jm_set_current_scope_var_fresh(JsMirTranspiler* mt, const char* name,
        MIR_reg_t reg, MIR_type_t mir_type, TypeId type_id) {
    struct hashmap* scope = jm_var_scope_at(mt, mt ? mt->scope_depth : -1);
    if (!mt || !name || mt->scope_depth < 0 || !scope) {
        return NULL;
    }
    JsVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.name = mir_em_persist_cstr(&mt->em, name).str;
    entry.var.reg = reg;
    entry.var.mir_type = mir_type;
    entry.var.type_id = type_id;
    jm_install_fresh_var_entry(mt, mt->scope_depth, &entry);

    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    key.name = name;
    JsVarScopeEntry* found = (JsVarScopeEntry*)hashmap_get(scope, &key);
    return found ? &found->var : NULL;
}

static JsAstNode* jm_find_pattern_binding_node(JsAstNode* pattern, const char* name) {
    if (!pattern || !name) return NULL;
    switch (pattern->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)pattern;
        if (!id->name) return NULL;
        const char* binding_name = jm_var_name(id->name);
        if (strcmp(binding_name, name) != 0) return NULL;
        // Capture analysis keys identifiers by their defining declarator range,
        // which can be wider than the identifier token itself.
        return id->entry && id->entry->node ? (JsAstNode*)id->entry->node : pattern;
    }
    case JS_AST_NODE_ARRAY_PATTERN:
    case JS_AST_NODE_ARRAY_EXPRESSION:
        for (JsAstNode* element = ((JsArrayNode*)pattern)->elements;
                element; element = element->next) {
            JsAstNode* found = jm_find_pattern_binding_node(element, name);
            if (found) return found;
        }
        return NULL;
    case JS_AST_NODE_OBJECT_PATTERN:
    case JS_AST_NODE_OBJECT_EXPRESSION:
        for (JsAstNode* property = ((JsObjectNode*)pattern)->properties;
                property; property = property->next) {
            JsAstNode* found = jm_find_pattern_binding_node(property, name);
            if (found) return found;
        }
        return NULL;
    case JS_AST_NODE_PROPERTY:
        return jm_find_pattern_binding_node(((JsPropertyNode*)pattern)->value, name);
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        return jm_find_pattern_binding_node(((JsAssignmentPatternNode*)pattern)->left, name);
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        return jm_find_pattern_binding_node(((JsSpreadElementNode*)pattern)->argument, name);
    default:
        return NULL;
    }
}

static JsAstNode* jm_find_block_lexical_binding_node(JsAstNode* block, const char* name) {
    if (!block || block->node_type != JS_AST_NODE_BLOCK_STATEMENT || !name) return NULL;
    for (JsAstNode* stmt = ((JsBlockNode*)block)->statements; stmt; stmt = stmt->next) {
        if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            JsVariableDeclarationNode* declaration = (JsVariableDeclarationNode*)stmt;
            if (declaration->kind != JS_VAR_LET && declaration->kind != JS_VAR_CONST) continue;
            for (JsAstNode* item = declaration->declarations; item; item = item->next) {
                if (item->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
                JsAstNode* found = jm_find_pattern_binding_node(
                    ((JsVariableDeclaratorNode*)item)->id, name);
                if (found) return found;
            }
        } else if (stmt->node_type == JS_AST_NODE_CLASS_DECLARATION) {
            JsClassNode* cls = (JsClassNode*)stmt;
            if (cls->name) {
                const char* binding_name = jm_var_name(cls->name);
                if (strcmp(binding_name, name) == 0) return stmt;
            }
        }
    }
    return NULL;
}

void jm_init_block_tdz(JsMirTranspiler* mt, JsAstNode* block) {
    if (!block || block->node_type != JS_AST_NODE_BLOCK_STATEMENT) return;
    struct hashmap* let_consts = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_let_const_names(block, let_consts);
    size_t iter = 0; void* item;
    while (hashmap_iter(let_consts, &iter, &item)) {
        JsNameSetEntry* e = (JsNameSetEntry*)item;
        if (e->from_func_decl) {
            continue;
        }
        MIR_reg_t tdz_reg = jm_new_reg(mt, e->name, MIR_T_I64);
        jm_emit_reg_op(mt, MIR_MOV, tdz_reg, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_TDZ));
        jm_set_var(mt, e->name, tdz_reg);
        JsAstNode* binding_node = jm_find_block_lexical_binding_node(block, e->name);
        JsMirVarEntry* ve = jm_find_var(mt, e->name);
        if (ve) {
            ve->is_let_const = true;
            ve->is_const = (e->var_kind == 2);  // JS_VAR_CONST
            ve->tdz_active = true;
            if (binding_node) {
                ve->binding_start = binding_node->source_span.start_byte;
                ve->binding_end = binding_node->source_span.end_byte;
            }
        }
        // A block lexical can shadow a parameter with the same source name.
        // Preserve the declaration range here so its TDZ sentinel cannot claim
        // the parameter's plain-name scope-env cell before initialization.
        if (binding_node) {
            jm_scope_env_mark_and_writeback_binding(mt, e->name, binding_node, tdz_reg);
        } else {
            jm_scope_env_mark_and_writeback(mt, e->name, tdz_reg);
        }
    }
    hashmap_free(let_consts);

    JsBlockNode* blk = (JsBlockNode*)block;
    JsAstNode* stmt = blk->statements;
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            JsFunctionNode* fn = (JsFunctionNode*)stmt;
            if (fn->name) {
                JsFuncCollected* fc = jm_find_collected_func(mt, fn);
                if (fc && fc->func_item) {
                    const char* vname = jm_var_name(fn->name);
                    MIR_reg_t binding_reg = jm_new_reg(mt, vname, MIR_T_I64);
                    jm_emit_reg_op(mt, MIR_MOV, binding_reg, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
                    JsMirVarEntry* ve = jm_set_current_scope_var_fresh(mt, vname, binding_reg, MIR_T_I64, LMD_TYPE_ANY);
                    if (ve) {
                        ve->is_let_const = true;
                        ve->tdz_active = false;
                        ve->from_block_func_decl = true;
                    }
                    MIR_reg_t fn_reg = jm_create_func_or_closure(mt, fc);
                    jm_emit_mov(mt, binding_reg, fn_reg);
                    jm_scope_env_mark_and_writeback(mt, vname, fn_reg);
                }
            }
        }
        stmt = stmt->next;
    }
}

void jm_init_switch_tdz(JsMirTranspiler* mt, JsAstNode* switch_node) {
    if (!switch_node || switch_node->node_type != JS_AST_NODE_SWITCH_STATEMENT) return;
    struct hashmap* let_consts = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_switch_lexical_names(switch_node, let_consts);
    int slot = 0;
    size_t iter = 0; void* item;
    while (hashmap_iter(let_consts, &iter, &item)) {
        JsNameSetEntry* e = (JsNameSetEntry*)item;
        MIR_reg_t tdz_reg = jm_new_reg(mt, e->name, MIR_T_I64);
        jm_emit_reg_op(mt, MIR_MOV, tdz_reg, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_TDZ));
        jm_set_var(mt, e->name, tdz_reg);
        JsMirVarEntry* ve = jm_find_var(mt, e->name);
        if (ve) {
            ve->is_let_const = true;
            ve->is_const = (e->var_kind == JS_VAR_CONST);
            ve->tdz_active = true;
            if (mt->scope_env_reg != 0) {
                ve->in_scope_env = true;
                ve->scope_env_slot = slot;
                ve->scope_env_reg = mt->scope_env_reg;
                jm_emit_store_i64(mt, slot * (int)sizeof(uint64_t), mt->scope_env_reg, tdz_reg);
            }
        }
        slot++;
    }
    hashmap_free(let_consts);

    JsSwitchNode* sw = (JsSwitchNode*)switch_node;
    for (JsAstNode* c = sw->cases; c; c = c->next) {
        if (c->node_type != JS_AST_NODE_SWITCH_CASE) continue;
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)c;
        for (JsAstNode* stmt = sc->consequent; stmt; stmt = stmt->next) {
            if (stmt->node_type != JS_AST_NODE_FUNCTION_DECLARATION) continue;
            JsFunctionNode* fn = (JsFunctionNode*)stmt;
            if (!fn->name) continue;
            JsFuncCollected* fc = jm_find_collected_func(mt, fn);
            if (!fc || !fc->func_item) continue;
            const char* vname = jm_var_name(fn->name);
            MIR_reg_t binding_reg = jm_new_reg(mt, vname, MIR_T_I64);
            jm_emit_reg_op(mt, MIR_MOV, binding_reg, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
            JsMirVarEntry* ve = jm_set_current_scope_var_fresh(mt, vname, binding_reg, MIR_T_I64, LMD_TYPE_ANY);
            if (ve) {
                ve->is_let_const = true;
                ve->tdz_active = false;
                ve->from_block_func_decl = true;
            }
            MIR_reg_t fn_reg = jm_create_func_or_closure(mt, fc);
            jm_emit_mov(mt, binding_reg, fn_reg);
            jm_scope_env_mark_and_writeback(mt, vname, fn_reg);
        }
    }
}

// Analyze captures for a function: find identifiers referenced but not locally declared
// Recursively collect variable names from a destructuring pattern into a name set.
// Handles: identifier, assignment_pattern (x ), object_pattern, array_pattern.
void jm_collect_pattern_names(JsAstNode* pat, struct hashmap* names) {
    if (!pat) return;
    switch (pat->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)pat;
        const char* name = jm_var_name(id->name);
        jm_name_set_add(names, name);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN: {
        // x = default: the name is the left-hand side
        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)pat;
        jm_collect_pattern_names(ap->left, names);
        break;
    }
    case JS_AST_NODE_OBJECT_PATTERN: {
        // { a, b: c, ...rest }
        JsObjectPatternNode* op = (JsObjectPatternNode*)pat;
        JsAstNode* prop = op->properties;
        while (prop) {
            if (prop->node_type == JS_AST_NODE_PROPERTY) {
                JsPropertyNode* p = (JsPropertyNode*)prop;
                // The binding is the value (for renaming: {a: b} → b is the param)
                jm_collect_pattern_names(p->value ? p->value : p->key, names);
            } else if (prop->node_type == JS_AST_NODE_REST_PROPERTY || prop->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* sp = (JsSpreadElementNode*)prop;
                jm_collect_pattern_names(sp->argument, names);
            } else {
                jm_collect_pattern_names(prop, names);
            }
            prop = prop->next;
        }
        break;
    }
    case JS_AST_NODE_ARRAY_PATTERN: {
        // [a, b, ...rest]
        JsArrayPatternNode* ap = (JsArrayPatternNode*)pat;
        JsAstNode* elem = ap->elements;
        while (elem) {
            jm_collect_pattern_names(elem, names);
            elem = elem->next;
        }
        break;
    }
    case JS_AST_NODE_SPREAD_ELEMENT:
    case JS_AST_NODE_REST_ELEMENT: {
        JsSpreadElementNode* sp = (JsSpreadElementNode*)pat;
        jm_collect_pattern_names(sp->argument, names);
        break;
    }
    default:
        break;
    }
}

// Indexed bindings make declarations and member/property names distinguishable
// without a second tree walk. Owner-function IDs also exclude nested closures.
static bool jm_index_identifier_is_binding(AstIndex* index, uint32_t node_id,
        JsIdentifierNode* id) {
    if (!index || !id || !id->entry || !id->entry->node) return false;
    AstNode* definition = (AstNode*)id->entry->node;
    if (definition == (AstNode*)id) return true;
    // Variable entries point at the whole declarator, whose initializer also
    // lies inside its span; only the pattern span is a binding position.
    if (definition->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
        JsVariableDeclaratorNode* declarator =
            (JsVariableDeclaratorNode*)definition;
        JsAstNode* pattern = declarator->id;
        return pattern && pattern->source_span.start_byte <= id->source_span.start_byte &&
            id->source_span.end_byte <= pattern->source_span.end_byte;
    }
    // Function/class entries cover their complete body; their declared name is
    // the sole binding identifier owned by that entry.
    if (definition->node_type == JS_AST_NODE_FUNCTION_DECLARATION ||
            definition->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
            definition->node_type == JS_AST_NODE_ARROW_FUNCTION ||
            definition->node_type == JS_AST_NODE_METHOD_DEFINITION) {
        JsFunctionNode* function = (JsFunctionNode*)definition;
        // Function names are stored as interned strings rather than AST name
        // nodes; the declared name is the only entry use before the body.
        return function->body && definition->source_span.start_byte <=
            id->source_span.start_byte && id->source_span.end_byte <=
            function->body->source_span.start_byte;
    }
    if (definition->node_type == JS_AST_NODE_CLASS_DECLARATION ||
            definition->node_type == JS_AST_NODE_CLASS_EXPRESSION) {
        JsClassNode* class_node = (JsClassNode*)definition;
        return class_node->body && definition->source_span.start_byte <=
            id->source_span.start_byte && id->source_span.end_byte <=
            class_node->body->source_span.start_byte;
    }
    // Parameter and catch bindings normally point directly at their pattern;
    // retain exact-span matching for parser placeholders without treating an
    // enclosing executable node as a declaration.
    return definition->source_span.start_byte == id->source_span.start_byte &&
        definition->source_span.end_byte == id->source_span.end_byte;
}

static bool jm_index_identifier_is_property_key(AstNode* parent, AstNode* node) {
    if (!parent || !node) return false;
    if (parent->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* member = (JsMemberNode*)parent;
        return !member->computed && member->property == node;
    }
    if (parent->node_type == JS_AST_NODE_PROPERTY) {
        JsPropertyNode* property = (JsPropertyNode*)parent;
        return !property->computed && property->key == node;
    }
    if (parent->node_type == JS_AST_NODE_METHOD_DEFINITION) {
        return ((JsMethodDefinitionNode*)parent)->key == node;
    }
    return false;
}

void jm_collect_indexed_body_refs(JsMirTranspiler* mt, JsFunctionNode* fn,
        struct hashmap* refs) {
    if (!mt || !fn || !refs || !mt->tp) return;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId fn_node_id = ast_index_find(index, (AstNode*)fn);
    AstFunctionId owner = fn_node_id == AST_NODE_ID_INVALID ?
        AST_FUNCTION_ID_INVALID : index->owner_functions[fn_node_id];
    if (owner == AST_FUNCTION_ID_INVALID) return;
    uint32_t body_start = fn->body ? fn->body->source_span.start_byte : 0;
    uint32_t body_end = fn->body ? fn->body->source_span.end_byte : 0;
    for (uint32_t i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (!node || index->owner_functions[i] != owner ||
                node->node_type != JS_AST_NODE_IDENTIFIER) continue;
        JsIdentifierNode* id = (JsIdentifierNode*)node;
        bool is_binding = jm_index_identifier_is_binding(index, i, id);
        AstNodeId parent_id = ast_index_parent_id(index, i);
        AstNode* parent = parent_id < index->count ? index->nodes[parent_id] : NULL;
        bool is_property_key = jm_index_identifier_is_property_key(parent, node);
        if (!id->name || is_binding || is_property_key) continue;
        if (parent && parent->node_type == JS_AST_NODE_MEMBER_EXPRESSION &&
                ((JsMemberNode*)parent)->object == node && id->name->len == 5 &&
                memcmp(id->name->chars, "super", 5) == 0) continue;
        if (parent && (parent->node_type == JS_AST_NODE_CALL_EXPRESSION ||
                parent->node_type == JS_AST_NODE_NEW_EXPRESSION) &&
                ((JsCallNode*)parent)->callee == node && id->name->len == 5 &&
                memcmp(id->name->chars, "super", 5) == 0) continue;
        jm_name_set_add_ref(refs, jm_var_name(id->name), id, body_start, body_end);
    }
}

static bool jm_analysis_function_is_method_syntax(JsFunctionNode* fn) {
    return fn && fn->node_type == JS_AST_NODE_METHOD_DEFINITION;
}

static void jm_add_capture(JsFuncCollected* fc, const char* name, NameEntry* entry,
                           bool is_nfe_binding, bool force_env_capture) {
    jm_ensure_captures_capacity(fc);
    FnCapture* capture = &JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)++];
    capture->name = jm_persist_name(name);
    capture->scope_env_key = jm_persist_name(name);
    capture->scope_env_slot = capture->private_env_slot = capture->grandparent_slot = -1;
    capture->parent_env_link_slot_override = -1;
    capture->entry = entry;
    capture->is_mutable = capture->is_let_const = capture->is_const = false;
    capture->is_nfe_binding = is_nfe_binding;
    capture->force_env_capture = force_env_capture;
}

void jm_analyze_captures(JsMirTranspiler* mt, JsFuncCollected* fc,
                         struct hashmap* outer_scope_names,
                         struct hashmap* module_consts,
                         struct hashmap* ancestor_func_locals,
                         bool captures_with_scope) {
    JsFunctionNode* fn = fc->node;
    FnAnalysis* analysis = jm_function_analysis(fc);
    if (!analysis) return;
    JM_CAPTURE_COUNT(fc) = 0;
    analysis->js_has_direct_eval = js_ast_function_has_direct_eval(fn);

    // Collect parameter names (handles simple ids, default params, and destructuring)
    struct hashmap* params = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    JsAstNode* param = fn->params;
    while (param) {
        jm_collect_pattern_names(param, params);
        param = param->next;
    }

    // Collect local variable declarations
    struct hashmap* locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    if (fn->body) jm_collect_indexed_body_locals(mt, fn->body, locals);

    // Collect all identifier references in the body
    struct hashmap* refs = hashmap_new(sizeof(JsNameSetEntry), 64, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_indexed_body_refs(mt, fn, refs);

    // Default initializers execute in the function environment and can read
    // this/new.target before the body; classify them before stamping call ABI
    // facts so a binding-oblivious lane cannot hide either value.
    uint8_t observations = js_ast_function_observation_mask(fn);
    JM_JS_FACT(fc, observes_this) = (observations & JS_AST_OBSERVES_THIS) ||
        JM_JS_FACT(fc, has_direct_eval);
    JM_JS_FACT(fc, observes_new_target) = (observations & JS_AST_OBSERVES_NEW_TARGET) ||
        JM_JS_FACT(fc, has_direct_eval);
    // A function nested below a `with` is created with that Object Environment
    // Record and must keep dynamic name lookup after the enclosing body returns.
    JM_JS_FACT(fc, uses_with) = captures_with_scope || JM_JS_FACT(fc, has_direct_eval) ||
        js_ast_function_has_with(fn);

    // Find captures: referenced identifiers that are not params/locals but ARE in outer scope
    // Track self-references separately — if the function has other captures (and thus
    // becomes a closure), it also needs to capture itself for recursive calls.
    const char* self_name = NULL;
    bool has_self_ref = false;
    if (fn->name) {
        self_name = jm_var_name(fn->name);
    }
    bool is_method_syntax = jm_analysis_function_is_method_syntax(fn);
    bool is_func_expr = fn->node_type == JS_AST_NODE_FUNCTION_EXPRESSION;

    size_t iter = 0;
    void* item;
    while (hashmap_iter(refs, &iter, &item)) {
        JsNameSetEntry* ref = (JsNameSetEntry*)item;
        // A parameter shadows every outer/self binding. Testing the function
        // name first made `function r(t, e, r)` overwrite the outer `r` cell
        // with its numeric argument and later attempt to call that number.
        if (jm_name_set_has(params, ref->name)) continue;    // local param
        // The AST now resolves an NFE self name to its private function scope,
        // but MIR still represents recursion through the closure environment.
        if (!JM_JS_FACT(fc, is_class_method) && !is_method_syntax &&
            self_name && self_name[0] && strcmp(ref->name, self_name) == 0) {
            has_self_ref = true;
            continue;
        }
        // A function-wide name set cannot distinguish an outer capture from a
        // same-named lexical declared later in a nested for/block. Binding
        // ranges identify declarations owned by this function without making
        // the earlier outer reference disappear from capture analysis.
        if (jm_ref_binding_is_inside_function(fn, ref)) continue;
        // for-of/in lexical heads are block-scoped; a same-named loop variable
        // must not mask an earlier outer binding captured before that block.
        if (jm_ref_is_local_binding(locals, ref)) continue;  // local var
        if (strcmp(ref->name, "_js_new.target") == 0) continue; // handled by arrow lexical capture below
        if (JsClassEntry* owner_class = jm_function_owner_class(mt, fc); owner_class &&
            owner_class->name && strlen(ref->name) == owner_class->name->len + 4 &&
            strncmp(ref->name, "_js_", 4) == 0 &&
            strncmp(ref->name + 4, owner_class->name->chars,
                owner_class->name->len) == 0) {
            // A named class's private self-name belongs to the class lexical
            // environment. Treating it as an outer capture makes propagation
            // demand a nonexistent binding from the enclosing expression scope.
            continue;
        }
        // Runtime globals belong to the realm environment, not to an esbuild
        // wrapper closure. Capturing `document` here stored an uninitialised
        // closure slot, so ordinary SVG feature detection later observed
        // `undefined` instead of the active document. A real ancestor local
        // still shadows the realm binding and must remain a capture.
        if (strncmp(ref->name, "_js_", 4) == 0 &&
            js_builtin_global_find(ref->name + 4, (int)strlen(ref->name + 4)) &&
            !(ancestor_func_locals && jm_name_set_has(ancestor_func_locals, ref->name))) {
            continue;
        }
        if (!jm_name_set_has(outer_scope_names, ref->name)) continue;  // not in outer scope
        // Skip module-level bindings; identifier lowering resolves them via
        // module_consts (and MCONST_MODVAR uses live js_get_module_var reads).
        // If a parent function declares a local with the same name, that local
        // shadows the module binding, so we still capture the parent binding.
        if (module_consts && !(ancestor_func_locals && jm_name_set_has(ancestor_func_locals, ref->name))) {
            JsModuleConstEntry* mc = jm_find_module_const_in(module_consts, ref->name);
            if (mc) continue;  // resolved via module_consts, no capture needed
        }
        // A parent-local binding shadows an IIFE-promoted module binding with
        // the same minified name. Force the closure cell path or later lowering
        // will incorrectly read/write the unrelated module const.
        // Direct Program declarations never enter ancestor_func_locals. Any
        // matching entry is therefore a real enclosing block/catch/loop or
        // function binding that shadows the same-named module cell, including
        // for a top-level closure with no parent FunctionId.
        bool force_env_capture = ancestor_func_locals &&
            jm_name_set_has(ancestor_func_locals, ref->name);
        bool is_lexical_for_head = jm_entry_is_lexical_for_head(ref->entry);

        // This is a capture
        {
            jm_ensure_captures_capacity(fc);
            JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].name = jm_persist_name(ref->name);
            if (ref->binding_start != 0 || ref->binding_end != 0) {
                JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].scope_env_key = jm_format_name(
                    "%s@%u:%u", ref->name, ref->binding_start, ref->binding_end);
            } else {
                JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].scope_env_key = jm_persist_name(ref->name);
            }
            JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].scope_env_slot = -1;
            JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].private_env_slot = -1;
            JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].grandparent_slot = -1;
            JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].parent_env_link_slot_override = -1;
            JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].entry = ref->entry;
            // Binding metadata is authoritative when scope analysis resolved
            // the reference; name-only ancestor scans can confuse an outer
            // const with a nearer same-named var in minified code.
            JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].is_let_const = ref->var_kind != 0 ||
                is_lexical_for_head;
            JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].is_const = ref->var_kind == JS_VAR_CONST;
            JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].is_nfe_binding = false;
            JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].force_env_capture = force_env_capture ||
                is_lexical_for_head;
            const char* capture_key = JM_CAPTURE_ARRAY(fc)[JM_CAPTURE_COUNT(fc)].scope_env_key;
            JM_CAPTURE_COUNT(fc)++;
            log_debug("js-mir: capture '%s' [%s] in function '%s'",
                ref->name, capture_key, fc->name);
        }
    }

    // If the function references itself (e.g., recursive calls, or Box2D constructor
    // pattern where F.method.apply(this, arguments) needs to find F), it must capture
    // itself so the reference resolves to the correct function at runtime.
    // This is critical when multiple IIFEs define functions with the same minified name
    // (e.g., 'r', 'K') — without self-capture, the module_consts table would conflate them.
    // Only add self-capture for non-top-level functions — top-level
    // function declarations are hoisted and uniquely resolve via module var table.
    // Keeping top-level functions capture-free preserves tail-call optimization.
    // Exception: function EXPRESSIONS always need self-capture for NFE name binding,
    // since their name is not in the module var table even when top-level.
    bool is_block_func_decl = fn->node_type == JS_AST_NODE_FUNCTION_DECLARATION &&
        !jm_function_decl_is_direct_binding(fn, false);
    if (has_self_ref && self_name && self_name[0] &&
            (jm_parent_function_id(mt, fc) != AST_FUNCTION_ID_INVALID || is_func_expr ||
             is_block_func_decl)) {
        // Annex B exposes a separate outer var binding for a block function.
        // Its self-reference must stay in the private block closure cell;
        // resolving it through the same-named module var lets `f = 123` inside
        // the function overwrite the callable outer binding.
        jm_add_capture(fc, self_name, NULL, is_func_expr, is_block_func_decl);
        log_debug("js-mir: self-capture '%s' in closure '%s'", self_name, fc->name);
    }

    // Arrow functions: capture 'this' from enclosing lexical scope.
    // In JS, arrow functions do NOT have their own 'this'; they inherit from the parent.
    if (fn->is_arrow && (observations & JS_AST_OBSERVES_THIS)) {
        jm_add_capture(fc, "_js_this", NULL, false, false);
        log_debug("js-mir: arrow capture '_js_this' in function '%s'", fc->name);
    }

    // Arrow functions also capture new.target lexically.  A normal direct call
    // clears the dynamic runtime new.target, so arrows must keep a closure slot
    // for the value visible where the arrow was created.
    if (fn->is_arrow && (observations & JS_AST_OBSERVES_NEW_TARGET)) {
        jm_add_capture(fc, "_js_new.target", NULL, false, false);
        log_debug("js-mir: arrow capture '_js_new.target' in function '%s'", fc->name);
    }

    if (fn->is_arrow && (observations & JS_AST_OBSERVES_ARGUMENTS)) {
        jm_add_capture(fc, "_js_arguments", NULL, false, false);
        log_debug("js-mir: arrow capture '_js_arguments' in function '%s'", fc->name);
    }

    // v18q: publish the shared AST fact; refs still carry lexical captures.
    JM_JS_FACT(fc, uses_arguments) = !fn->is_arrow && js_ast_function_uses_arguments(fn);
    analysis->captures = JM_CAPTURE_ARRAY(fc);
    analysis->capture_count = JM_CAPTURE_COUNT(fc);

    hashmap_free(params);
    hashmap_free(locals);
    hashmap_free(refs);
}
