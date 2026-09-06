#include "js_mir_internal.hpp"

uint64_t jm_name_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JsNameSetEntry* e = (const JsNameSetEntry*)item;
    return hashmap_sip(e->name, strlen(e->name), seed0, seed1);
}

int jm_name_cmp(const void* a, const void* b, void* udata) {
    return strcmp(((const JsNameSetEntry*)a)->name, ((const JsNameSetEntry*)b)->name);
}

int jm_binding_cmp(const void* a, const void* b, void* udata) {
    const JsNameSetEntry* lhs = (const JsNameSetEntry*)a;
    const JsNameSetEntry* rhs = (const JsNameSetEntry*)b;
    if (lhs->entry || rhs->entry) return lhs->entry == rhs->entry ? 0 : 1;
    return jm_name_cmp(a, b, udata);
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

bool jm_entry_is_owned_by_function(const JsFunctionNode* function,
        const NameEntry* entry) {
    if (!function || !function->vars || !entry || !entry->scope) return false;
    for (const JsScope* scope = entry->scope; scope; scope = scope->parent) {
        if (scope == function->vars) return true;
        if (scope->kind == SCOPE_KIND_FUNCTION || scope->kind == SCOPE_KIND_GLOBAL ||
                scope->kind == SCOPE_KIND_MODULE) return false;
    }
    return false;
}

static bool jm_entry_is_enclosing_nonmodule_binding(const JsFunctionNode* function,
        const NameEntry* entry) {
    if (!function || !function->vars || !entry || !entry->scope) return false;
    for (const JsScope* scope = function->vars->parent; scope; scope = scope->parent) {
        if (scope->kind == SCOPE_KIND_GLOBAL || scope->kind == SCOPE_KIND_MODULE) return false;
        if (scope == entry->scope) return true;
    }
    return false;
}

static bool jm_entry_is_promoted_iife_binding(JsMirTranspiler* mt,
        JsFuncCollected* function, const char* name, const NameEntry* entry) {
    JsModuleConstEntry* module_entry = mt && mt->module_consts
        ? jm_find_module_const_in(mt->module_consts, name) : NULL;
    if (!module_entry || !(module_entry->is_iife_var ||
            module_entry->is_iife_func_decl)) return false;
    for (JsFuncCollected* ancestor = jm_parent_collected_func(mt, function);
            ancestor; ancestor = jm_parent_collected_func(mt, ancestor)) {
        if (JM_JS_FACT(ancestor, is_iife_body) &&
                jm_entry_is_owned_by_function(ancestor->node, entry)) return true;
    }
    return false;
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

static void jm_name_set_add_binding(struct hashmap* set, const char* name,
        JsAstNode* binding_node, int var_kind = 0, NameEntry* entry = NULL) {
    JsNameSetEntry e;
    memset(&e, 0, sizeof(e));
    e.name = jm_persist_name(name);
    e.var_kind = var_kind;
    e.entry = entry;
    e.binding_node = binding_node;
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
            existing->binding_node = e.binding_node;
        }
        if (!existing->entry && e.entry) existing->entry = e.entry;
        if (existing->var_kind == 0 && e.var_kind != 0)
            existing->var_kind = e.var_kind;
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

bool jm_binding_set_has(struct hashmap* set, NameEntry* binding) {
    JsNameSetEntry key = {};
    key.name = binding && binding->name ? jm_var_name(binding->name) : "";
    key.entry = binding;
    return hashmap_get(set, &key) != NULL;
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
        if (id && id->name) {
            jm_name_set_add_binding(names, jm_var_name(id->name),
                (JsAstNode*)id, 0, id->entry);
        }
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
            // Direct scope binding publishes the declaration identity before
            // indexed collection. Re-scanning its parent scope by node pointer
            // can select an Annex B companion instead of the call-site binding.
            NameEntry* binding = fn->entry;
            JsNameSetEntry entry;
            memset(&entry, 0, sizeof(entry));
            entry.name = jm_persist_name(jm_var_name(fn->name));
            entry.from_func_decl = true;
            entry.entry = binding;
            JsNameSetEntry* existing = (JsNameSetEntry*)hashmap_get(context->locals, &entry);
            if (!existing) hashmap_set(context->locals, &entry);
            else if (!existing->entry && binding) existing->entry = binding;
        }
    } else if (!context->var_only && current->node_type == JS_AST_NODE_CLASS_DECLARATION) {
        JsClassNode* cls = (JsClassNode*)current;
        if (cls->name) {
            jm_name_set_add_binding(context->locals, jm_var_name(cls->name),
                (JsAstNode*)cls, JS_VAR_LET, cls->outer_entry);
        }
    } else if (current->node_type == JS_AST_NODE_FOR_OF_STATEMENT ||
            current->node_type == JS_AST_NODE_FOR_IN_STATEMENT) {
        JsForOfNode* loop = (JsForOfNode*)current;
        if (!loop->left) return true;
        if (loop->left->node_type == JS_AST_NODE_IDENTIFIER) {
            if (!context->var_only || loop->kind == JS_VAR_VAR) {
                if (loop->kind == JS_VAR_LET || loop->kind == JS_VAR_CONST) {
                    JsIdentifierNode* id = (JsIdentifierNode*)loop->left;
                    jm_name_set_add_binding(context->locals, jm_var_name(id->name),
                        loop->left, loop->kind, id->entry);
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

// Binding runs before MIR planning, so a scope is the single direct-declaration
// authority for TDZ initialization. Re-walking statements and patterns here
// duplicated that binding work and could choose a different destructuring span.
static void jm_collect_scope_lexical_names(JsScope* scope,
        struct hashmap* names) {
    if (!scope || !names) return;
    for (NameEntry* binding = scope->first; binding; binding = binding->next) {
        if (!binding->is_lexical || !binding->name) continue;
        JsNameSetEntry entry = {};
        entry.name = jm_persist_name(jm_var_name(binding->name));
        entry.var_kind = binding->is_const ? JS_VAR_CONST : JS_VAR_LET;
        entry.entry = binding;
        entry.binding_node = (JsAstNode*)binding->node;
        entry.from_func_decl = binding->node &&
            binding->node->node_type == JS_AST_NODE_FUNCTION_DECLARATION;
        hashmap_set(names, &entry);
    }
}

void jm_collect_let_const_names(JsAstNode* block, struct hashmap* names) {
    if (!block || block->node_type != JS_AST_NODE_BLOCK_STATEMENT) return;
    jm_collect_scope_lexical_names(((JsBlockNode*)block)->vars, names);
}

void jm_collect_switch_lexical_names(JsAstNode* switch_node, struct hashmap* names) {
    if (!switch_node || switch_node->node_type != JS_AST_NODE_SWITCH_STATEMENT) return;
    jm_collect_scope_lexical_names(((JsSwitchNode*)switch_node)->vars, names);
}

bool jm_function_decl_annex_b_disallowed(const JsNameSetEntry* entry) {
    return entry && entry->from_func_decl && entry->entry &&
        entry->entry->is_lexical && !entry->entry->annex_b_outer_binding;
}

// v20 TDZ: Initialize let/const variables in a block to TDZ sentinel.
// Call at block entry (after jm_push_scope) before transpiling block statements.
static JsMirVarEntry* jm_set_current_scope_var_fresh(JsMirTranspiler* mt, const char* name,
        MIR_reg_t reg, MIR_type_t mir_type, TypeId type_id, NameEntry* binding) {
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
    entry.var.binding = binding;
    jm_install_fresh_var_entry(mt, mt->scope_depth, &entry);
    return jm_find_var_at(mt, name, mt->scope_depth);
}

static void jm_init_block_function_binding(JsMirTranspiler* mt,
        JsFunctionNode* function) {
    if (!function || !function->name) return;
    JsFuncCollected* collected = jm_find_collected_func(mt, function);
    if (!collected || !collected->func_item) return;
    const char* name = jm_var_name(function->name);
    MIR_reg_t binding = jm_new_reg(mt, name, MIR_T_I64);
    jm_emit_reg_op(mt, MIR_MOV, binding,
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
    JsMirVarEntry* variable = jm_set_current_scope_var_fresh(mt, name,
        binding, MIR_T_I64, LMD_TYPE_ANY, function->entry);
    if (variable) {
        variable->is_let_const = true;
        variable->tdz_active = false;
        variable->from_block_func_decl = true;
    }
    MIR_reg_t closure = jm_create_func_or_closure(mt, collected);
    jm_emit_mov(mt, binding, closure);
    jm_scope_env_mark_and_writeback_binding(mt, name, (JsAstNode*)function,
        closure);
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
        jm_set_var(mt, e->name, tdz_reg, MIR_T_I64, LMD_TYPE_ANY, e->entry);
        JsAstNode* binding_node = e->binding_node;
        JsMirVarEntry* ve = jm_find_var_by_binding(mt, e->entry);
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
        jm_scope_env_mark_and_writeback_entry(mt, e->name, e->entry, tdz_reg);
    }
    hashmap_free(let_consts);

    JsBlockNode* blk = (JsBlockNode*)block;
    JsAstNode* stmt = blk->statements;
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            jm_init_block_function_binding(mt, (JsFunctionNode*)stmt);
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
        jm_set_var(mt, e->name, tdz_reg, MIR_T_I64, LMD_TYPE_ANY, e->entry);
        JsMirVarEntry* ve = jm_find_var_by_binding(mt, e->entry);
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
            jm_init_block_function_binding(mt, (JsFunctionNode*)stmt);
        }
    }
}

// Analyze captures for a function: find identifiers referenced but not locally declared
static void jm_collect_pattern_name_child(JsAstNode* child, void* opaque) {
    jm_collect_pattern_names(child, (struct hashmap*)opaque);
}

// Recursively collect variable names from a destructuring pattern into a name set.
// Handles: identifier, assignment_pattern (x ), object_pattern, array_pattern.
void jm_collect_pattern_names(JsAstNode* pat, struct hashmap* names) {
    if (!pat) return;
    switch (pat->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)pat;
        const char* name = jm_var_name(id->name);
        jm_name_set_add_binding(names, name, (JsAstNode*)id, 0, id->entry);
        break;
    }
    case JS_AST_NODE_PROPERTY:
        if (!((JsPropertyNode*)pat)->value) {
            jm_collect_pattern_names(((JsPropertyNode*)pat)->key, names);
            break;
        }
        [[fallthrough]];
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
    case JS_AST_NODE_OBJECT_PATTERN:
    case JS_AST_NODE_ARRAY_PATTERN:
    case JS_AST_NODE_SPREAD_ELEMENT:
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
        js_ast_visit_binding_pattern_children(pat,
            jm_collect_pattern_name_child, names);
        break;
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
        if (fn->source_span.end_byte > fn->source_span.start_byte &&
                (node->source_span.start_byte < fn->source_span.start_byte ||
                 node->source_span.end_byte > fn->source_span.end_byte)) {
            // The index can retain an owner label from a shared list edge;
            // source containment keeps a sibling's reference out of this closure.
            continue;
        }
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

static bool jm_capture_binding_is_lexical_ancestor(JsMirTranspiler* mt,
        JsFuncCollected* fc, NameEntry* entry) {
    if (!mt || !mt->tp || !fc || !entry || !entry->node) return true;
    if (jm_entry_is_enclosing_nonmodule_binding(fc->node, entry)) {
        // Module-level block closures have no structural function parent, but
        // their direct-scope edge still identifies the enclosing lexical cell.
        return true;
    }
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId binding_id = ast_index_find(index, (AstNode*)entry->node);
    if (binding_id == AST_NODE_ID_INVALID || binding_id >= index->count) return true;
    AstFunctionId binding_owner = index->owner_functions[binding_id];
    if (((JsAstNode*)entry->node)->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
        // A declaration node starts a new indexed function owner, but its name
        // binds in the enclosing lexical function.
        AstNodeId parent_id = ast_index_parent_id(index, binding_id);
        if (parent_id != AST_NODE_ID_INVALID && parent_id < index->count) {
            binding_owner = index->owner_functions[parent_id];
        }
    }
    if (binding_owner == AST_FUNCTION_ID_INVALID) return true;
    for (JsFuncCollected* ancestor = jm_parent_collected_func(mt, fc);
            ancestor; ancestor = jm_parent_collected_func(mt, ancestor)) {
        if (ancestor->function_id == binding_owner) return true;
    }
    return false;
}

void jm_analyze_captures(JsMirTranspiler* mt, JsFuncCollected* fc,
                         struct hashmap* module_consts,
                         bool captures_with_scope) {
    JsFunctionNode* fn = fc->node;
    FnAnalysis* analysis = jm_function_analysis(fc);
    if (!analysis) return;
    JM_CAPTURE_COUNT(fc) = 0;
    JsAstFunctionFacts facts = js_ast_collect_function_facts(fn->params, fn->body);
    analysis->js_has_direct_eval = facts.has_direct_eval;
    analysis->js_has_direct_super_call = facts.has_direct_super_call;
    analysis->js_first_direct_super_call_start = facts.first_direct_super_call_start;

    // Collect all identifier references in the body
    struct hashmap* refs = hashmap_new(sizeof(JsNameSetEntry), 64, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_indexed_body_refs(mt, fn, refs);

    // Default initializers execute in the function environment and can read
    // this/new.target before the body; classify them before stamping call ABI
    // facts so a binding-oblivious lane cannot hide either value.
    JM_JS_FACT(fc, observes_this) = (facts.observations & JS_AST_OBSERVES_THIS) ||
        JM_JS_FACT(fc, has_direct_eval);
    JM_JS_FACT(fc, observes_new_target) = (facts.observations & JS_AST_OBSERVES_NEW_TARGET) ||
        JM_JS_FACT(fc, has_direct_eval);
    // A function nested below a `with` is created with that Object Environment
    // Record and must keep dynamic name lookup after the enclosing body returns.
    JM_JS_FACT(fc, uses_with) = captures_with_scope || JM_JS_FACT(fc, has_direct_eval) ||
        facts.has_with;

    // Find captures: referenced identifiers that are not params/locals but ARE in outer scope
    // Track self-references separately — if the function has other captures (and thus
    // becomes a closure), it also needs to capture itself for recursive calls.
    bool has_self_ref = false;
    const char* self_name = fn->name ? jm_var_name(fn->name) : NULL;
    bool is_method_syntax = jm_analysis_function_is_method_syntax(fn);
    bool is_func_expr = fn->node_type == JS_AST_NODE_FUNCTION_EXPRESSION;

    size_t iter = 0;
    void* item;
    while (hashmap_iter(refs, &iter, &item)) {
        JsNameSetEntry* ref = (JsNameSetEntry*)item;
        // The direct scope pass resolves each capture candidate once. Unbound
        // names are realm/global reads, not closure slots.
        if (!ref->entry) continue;
        bool local_binding = jm_entry_is_owned_by_function(fn, ref->entry);
        bool enclosing_binding = jm_entry_is_enclosing_nonmodule_binding(fn, ref->entry) &&
            !jm_entry_is_promoted_iife_binding(mt, fc, ref->name, ref->entry);
        // The AST now resolves an NFE self name to its private function scope,
        // but MIR still represents recursion through the closure environment.
        if (!JM_JS_FACT(fc, is_class_method) && !is_method_syntax &&
            ref->entry == fn->entry && !local_binding) {
            has_self_ref = true;
            continue;
        }
        if (local_binding) continue;
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
            !enclosing_binding) {
            continue;
        }
        // A same-named declaration in a sibling function is not an outer
        // binding; leave the identifier on the realm-property lookup path.
        if (ref->entry && !jm_capture_binding_is_lexical_ancestor(mt, fc, ref->entry)) {
            continue;
        }
        // Skip module-level bindings; identifier lowering resolves them via
        // module_consts (and MCONST_MODVAR uses live js_get_module_var reads).
        // If a parent function declares a local with the same name, that local
        // shadows the module binding, so we still capture the parent binding.
        if (module_consts && !enclosing_binding) {
            JsModuleConstEntry* mc = jm_find_module_const_in(module_consts, ref->name);
            if (mc) continue;  // resolved via module_consts, no capture needed
        }
        // A parent-local binding shadows an IIFE-promoted module binding with
        // the same minified name. Force the closure cell path or later lowering
        // will incorrectly read/write the unrelated module const.
        // A resolved non-module lexical needs its own environment cell even
        // when its spelling also names a module variable. Its source key keeps
        // sibling closures on the same binding without an AST-wide name scan.
        bool force_env_capture = enclosing_binding;
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
        // The self reference resolves to the declaration/NFE binding already
        // published by direct scope. Preserve it in the synthetic capture so
        // body reads match the closure cell by identity.
        jm_add_capture(fc, self_name, fn->entry, is_func_expr,
            is_block_func_decl);
        log_debug("js-mir: self-capture '%s' in closure '%s'", self_name, fc->name);
    }

    // Arrow functions: capture 'this' from enclosing lexical scope.
    // In JS, arrow functions do NOT have their own 'this'; they inherit from the parent.
    if (fn->is_arrow && (facts.observations & JS_AST_OBSERVES_THIS)) {
        jm_add_capture(fc, "_js_this", NULL, false, false);
        log_debug("js-mir: arrow capture '_js_this' in function '%s'", fc->name);
    }

    // Arrow functions also capture new.target lexically.  A normal direct call
    // clears the dynamic runtime new.target, so arrows must keep a closure slot
    // for the value visible where the arrow was created.
    if (fn->is_arrow && (facts.observations & JS_AST_OBSERVES_NEW_TARGET)) {
        jm_add_capture(fc, "_js_new.target", NULL, false, false);
        log_debug("js-mir: arrow capture '_js_new.target' in function '%s'", fc->name);
    }

    if (fn->is_arrow && (facts.observations & JS_AST_OBSERVES_ARGUMENTS)) {
        jm_add_capture(fc, "_js_arguments", NULL, false, false);
        log_debug("js-mir: arrow capture '_js_arguments' in function '%s'", fc->name);
    }

    // v18q: publish the shared AST fact from the scan already used for captures.
    JM_JS_FACT(fc, uses_arguments) = !fn->is_arrow &&
        (facts.observations & JS_AST_OBSERVES_ARGUMENTS);
    analysis->captures = JM_CAPTURE_ARRAY(fc);
    analysis->capture_count = JM_CAPTURE_COUNT(fc);

    hashmap_free(refs);
}
