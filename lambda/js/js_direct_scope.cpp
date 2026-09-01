#include "js_transpiler.hpp"
#include "js_c_ast_helpers.hpp"
#include "../ts/ts_ast.hpp"
#include "../../lib/mempool.h"

// The direct parser reduces children before their enclosing function or block
// exists. This pass reconstructs the binding graph from the retained AST so
// identifier entries describe lexical reality rather than reduction order.

namespace {

static bool direct_is_function(const JsAstNode* node) {
    if (!node) return false;
    return node->node_type == JS_AST_NODE_FUNCTION_DECLARATION ||
        node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
        node->node_type == JS_AST_NODE_ARROW_FUNCTION;
}

static bool direct_is_class(const JsAstNode* node) {
    return node && (node->node_type == JS_AST_NODE_CLASS_DECLARATION ||
        node->node_type == JS_AST_NODE_CLASS_EXPRESSION);
}

static void direct_walk_node(JsTranspiler* tp, JsAstNode* node);
static void direct_walk_list(JsTranspiler* tp, JsAstNode* node);
static void direct_walk_block(JsTranspiler* tp, JsBlockNode* block,
        JsScopeType scope_type, bool is_function_body);
static void direct_predeclare_vars(JsTranspiler* tp, JsAstNode* node);
static void direct_predeclare_scope(JsTranspiler* tp, JsAstNode* node);

static void direct_link_interp_import_binding(JsTranspiler* tp,
        String* source, String* local_name, NameEntry* entry) {
    if (!tp || !source || !local_name || !entry) return;
    for (JsInterpImportBinding* binding = tp->interp_imports; binding;
            binding = binding->next) {
        // Both fields are parser-owned name-pool identities, not spellings.
        if (binding->source == source && binding->local_name == local_name) {
            binding->entry = entry;
        }
    }
}

static void direct_define_import_bindings(JsTranspiler* tp,
        JsImportNode* import_node) {
    if (!tp || !import_node) return;
    if (import_node->default_name) {
        import_node->default_entry = js_scope_define(tp,
            import_node->default_name, (JsAstNode*)import_node, JS_VAR_CONST);
        direct_link_interp_import_binding(tp, import_node->source,
            import_node->default_name, import_node->default_entry);
    }
    if (import_node->namespace_name) {
        import_node->namespace_entry = js_scope_define(tp,
            import_node->namespace_name, (JsAstNode*)import_node, JS_VAR_CONST);
        direct_link_interp_import_binding(tp, import_node->source,
            import_node->namespace_name, import_node->namespace_entry);
    }
    for (JsAstNode* spec = import_node->specifiers; spec; spec = spec->next) {
        if (spec->node_type != JS_AST_NODE_IMPORT_SPECIFIER) continue;
        JsImportSpecifierNode* import_spec = (JsImportSpecifierNode*)spec;
        if (import_spec->local_name) {
            import_spec->local_entry = js_scope_define(tp,
                import_spec->local_name, spec, JS_VAR_CONST);
            direct_link_interp_import_binding(tp, import_node->source,
                import_spec->local_name, import_spec->local_entry);
        }
    }
}

static void direct_walk_child(JsAstNode* child, void* opaque) {
    direct_walk_node((JsTranspiler*)opaque, child);
}

static void direct_set_identifier(JsTranspiler* tp, JsIdentifierNode* id,
        NameEntry* entry) {
    if (!id) return;
    id->entry = entry;
    id->type = entry && entry->node && entry->node->type
        ? entry->node->type : js_set_type_any(tp, ANY_OPEN_PARAM);
}

// Scope rebuilding shares the pattern shape while each mode retains its
// distinct identifier, initializer, and object-rest semantics.
enum DirectPatternWalkMode {
    DIRECT_PATTERN_BIND,
    DIRECT_PATTERN_DEFAULTS,
    DIRECT_PATTERN_ASSIGNMENT,
    DIRECT_PATTERN_DEFINE,
};

struct DirectPatternWalk {
    JsTranspiler* tp;
    JsVarKind kind;
    JsAstNode* owner;
    JsAstNode* declarator_owner;
    DirectPatternWalkMode mode;
    bool is_parameter;
    bool is_for_in_head;
    bool rest_binding;
};

static void direct_walk_pattern(DirectPatternWalk walk, JsAstNode* pattern) {
    if (!walk.tp || !pattern) return;
    if (pattern->node_type == (JsAstNodeType)TS_AST_NODE_PARAMETER) {
        TsParameterNode* parameter = (TsParameterNode*)pattern;
        if (walk.mode == DIRECT_PATTERN_ASSIGNMENT) {
            direct_walk_node(walk.tp, pattern);
            return;
        }
        walk.owner = NULL;
        direct_walk_pattern(walk, parameter->pattern);
        if (walk.mode == DIRECT_PATTERN_DEFAULTS) {
            direct_walk_node(walk.tp, parameter->default_value);
        }
        return;
    }
    switch (pattern->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)pattern;
        if (walk.mode == DIRECT_PATTERN_BIND) {
            NameEntry* entry = js_scope_lookup_current(walk.tp, id->name);
            if (!entry) entry = js_scope_define(walk.tp, id->name,
                walk.owner ? walk.owner : pattern, walk.kind);
            id->entry = entry;
            if (entry) {
                entry->is_parameter = entry->is_parameter || walk.is_parameter;
                entry->is_for_in_head = entry->is_for_in_head || walk.is_for_in_head;
            }
            // Binding identifiers are not reads. Rebuild their open parameter
            // state after the parser-time scope has been discarded; otherwise a
            // same-named declaration from an unrelated enclosing construct leaks
            // into the declaration's static type.
            id->type = js_set_type_any(walk.tp, ANY_OPEN_PARAM);
        } else if (walk.mode == DIRECT_PATTERN_ASSIGNMENT) {
            direct_set_identifier(walk.tp, id, js_scope_lookup(walk.tp, id->name));
        } else if (walk.mode == DIRECT_PATTERN_DEFINE) {
            JsAstNode* binding_node = walk.owner ? walk.owner : pattern;
            if (walk.rest_binding && walk.declarator_owner) {
                JsIdentifierNode* placeholder = (JsIdentifierNode*)pool_alloc(
                    walk.tp->pool, sizeof(JsIdentifierNode));
                memset(placeholder, 0, sizeof(JsIdentifierNode));
                placeholder->node_type = JS_AST_NODE_IDENTIFIER;
                placeholder->source_span = walk.declarator_owner->source_span;
                placeholder->name = id->name;
                placeholder->type = &TYPE_ANY;
                binding_node = (JsAstNode*)placeholder;
            }
            NameEntry* entry = js_scope_define(walk.tp, id->name, binding_node,
                walk.kind);
            // Every declaration pattern publishes its resolved binding. Later
            // MIR module/local registration must use this identity rather than
            // recovering a same-spelled name from a scope map.
            id->entry = entry;
        }
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN: {
        DirectPatternWalk child = walk;
        child.owner = NULL;
        direct_walk_pattern(child, ((JsAssignmentPatternNode*)pattern)->left);
        if (walk.mode == DIRECT_PATTERN_DEFAULTS ||
                walk.mode == DIRECT_PATTERN_ASSIGNMENT) {
            direct_walk_node(walk.tp, ((JsAssignmentPatternNode*)pattern)->right);
        }
        break;
    }
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT: {
        if (walk.mode == DIRECT_PATTERN_BIND &&
                pattern->node_type == JS_AST_NODE_SPREAD_ELEMENT) break;
        DirectPatternWalk child = walk;
        child.owner = NULL;
        if (walk.mode == DIRECT_PATTERN_DEFINE &&
                (pattern->node_type == JS_AST_NODE_REST_PROPERTY ||
                 pattern->node_type == JS_AST_NODE_SPREAD_ELEMENT)) {
            child.rest_binding = true;
        }
        direct_walk_pattern(child, ((JsSpreadElementNode*)pattern)->argument);
        break;
    }
    case JS_AST_NODE_ARRAY_PATTERN: {
        for (JsAstNode* item = ((JsArrayPatternNode*)pattern)->elements;
                item; item = item->next) {
            DirectPatternWalk child = walk;
            child.owner = NULL;
            if (walk.mode == DIRECT_PATTERN_DEFINE) child.rest_binding = false;
            direct_walk_pattern(child, item);
        }
        break;
    }
    case JS_AST_NODE_OBJECT_PATTERN: {
        for (JsAstNode* item = ((JsObjectPatternNode*)pattern)->properties;
                item; item = item->next) {
            DirectPatternWalk child = walk;
            child.owner = NULL;
            if (walk.mode == DIRECT_PATTERN_DEFINE) child.rest_binding = false;
            if (item->node_type == JS_AST_NODE_PROPERTY) {
                JsPropertyNode* property = (JsPropertyNode*)item;
                if (walk.mode == DIRECT_PATTERN_BIND ||
                        walk.mode == DIRECT_PATTERN_ASSIGNMENT) {
                    if (property->computed) direct_walk_node(walk.tp, property->key);
                    else if (property->key && property->key->node_type ==
                            JS_AST_NODE_IDENTIFIER) {
                        JsIdentifierNode* key = (JsIdentifierNode*)property->key;
                        key->entry = NULL;
                        key->type = &TYPE_ANY;
                    }
                }
                direct_walk_pattern(child, property->value);
            } else {
                direct_walk_pattern(child, item);
            }
        }
        break;
    }
    default:
        if (walk.mode == DIRECT_PATTERN_ASSIGNMENT) {
            direct_walk_node(walk.tp, pattern);
        }
        break;
    }
}

static void direct_bind_pattern(JsTranspiler* tp, JsAstNode* pattern,
        JsVarKind kind, JsAstNode* owner, bool is_parameter,
        bool is_for_in_head) {
    direct_walk_pattern({tp, kind, owner, NULL, DIRECT_PATTERN_BIND,
        is_parameter, is_for_in_head, false}, pattern);
}

static void direct_walk_pattern_defaults(JsTranspiler* tp, JsAstNode* pattern) {
    direct_walk_pattern({tp, JS_VAR_VAR, NULL, NULL, DIRECT_PATTERN_DEFAULTS,
        false, false, false}, pattern);
}

static void direct_walk_assignment_pattern(JsTranspiler* tp,
        JsAstNode* pattern) {
    direct_walk_pattern({tp, JS_VAR_VAR, NULL, NULL,
        DIRECT_PATTERN_ASSIGNMENT, false, false, false}, pattern);
}

static void direct_define_pattern(JsTranspiler* tp, JsAstNode* pattern,
        JsVarKind kind, JsAstNode* owner, JsAstNode* declarator_owner,
        bool rest_binding) {
    direct_walk_pattern({tp, kind, owner, declarator_owner,
        DIRECT_PATTERN_DEFINE, false, false, rest_binding}, pattern);
}

static void direct_define_variable(JsTranspiler* tp,
        JsVariableDeclarationNode* declaration) {
    if (!declaration) return;
    JsVarKind kind = (JsVarKind)declaration->kind;
    for (JsAstNode* item = declaration->declarations; item;
            item = item->next) {
        if (item->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
        JsVariableDeclaratorNode* declarator =
            (JsVariableDeclaratorNode*)item;
        direct_define_pattern(tp, declarator->id, kind, item, item, false);
    }
}

static void direct_define_function(JsTranspiler* tp, JsFunctionNode* function,
        JsScopeType scope_type) {
    if (!function || !function->name) return;
    JsVarKind kind = scope_type == JS_SCOPE_BLOCK &&
            !(tp->current_scope && tp->current_scope->is_function_body)
        ? JS_VAR_LET : JS_VAR_VAR;
    NameEntry* lexical = js_scope_define(tp, function->name,
        (JsAstNode*)function, kind);
    // The declaration node is the stable source owner for every later
    // collection and lowering pass. A later lookup runs inside the child
    // function scope and cannot recover this enclosing binding (D8.2.4).
    function->entry = lexical;
    if (lexical && !lexical->is_lexical) {
        // GlobalDeclarationInstantiation selects the final same-named function
        // declaration; keep the shared var binding's owner on that declaration.
        lexical->node = (AstNode*)function;
    }
    if (tp->current_scope && tp->current_scope->is_function_body &&
            lexical && !lexical->is_lexical) {
        // FunctionDeclarationInstantiation replaces an existing parameter or
        // var carrier with the hoisted function before body statements run.
        lexical->node = (AstNode*)function;
        return;
    }
    if (scope_type != JS_SCOPE_BLOCK || !lexical || !lexical->is_lexical) return;

    // Annex B.3.3 publishes a sloppy block function into the nearest
    // function/global var environment only after the block executes. Keep
    // that companion linked to the lexical declaration so the interpreter
    // can perform the delayed publication at block completion.
    lexical->annex_b_outer_binding = NULL;
    if ((tp->current_scope && tp->current_scope->strict) ||
            function->is_async || function->is_generator) return;
    bool var_conflict = false;
    for (JsScope* outer = tp->current_scope
            ? tp->current_scope->parent : NULL; outer; outer = outer->parent) {
        NameEntry* conflict = NULL;
        for (NameEntry* candidate = outer->first; candidate;
                candidate = candidate->next) {
            if (candidate->is_lexical && candidate->name &&
                    candidate->name->len == function->name->len &&
                    memcmp(candidate->name->chars, function->name->chars,
                        function->name->len) == 0) {
                conflict = candidate;
                break;
            }
        }
        if (conflict && !outer->allows_legacy_var_redeclaration) {
            var_conflict = true;
            break;
        }
        if (outer->kind == SCOPE_KIND_FUNCTION ||
                outer->kind == SCOPE_KIND_GLOBAL) break;
    }
    if (var_conflict) return;

    JsScope* var_scope = tp->current_scope;
    while (var_scope && var_scope->kind == SCOPE_KIND_BLOCK) {
        var_scope = var_scope->parent;
    }
    if (var_scope && var_scope->kind == SCOPE_KIND_FUNCTION &&
            var_scope->has_implicit_arguments &&
            function->name->len == 9 &&
            memcmp(function->name->chars, "arguments", 9) == 0) return;
    JsIdentifierNode* placeholder = (JsIdentifierNode*)pool_alloc(
        tp->pool, sizeof(JsIdentifierNode));
    if (!placeholder) return;
    memset(placeholder, 0, sizeof(JsIdentifierNode));
    placeholder->node_type = JS_AST_NODE_IDENTIFIER;
    placeholder->source_span = function->source_span;
    placeholder->name = function->name;
    placeholder->type = &TYPE_FUNC;
    NameEntry* outer = js_scope_define_in_scope(tp, var_scope,
        function->name, (JsAstNode*)placeholder, JS_VAR_VAR);
    if (outer && !outer->is_parameter) {
        lexical->annex_b_outer_binding = outer;
    }
}

static void direct_predeclare_one(JsTranspiler* tp, JsAstNode* node) {
    if (!tp || !node) return;
    if (node->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
        direct_predeclare_one(tp, ((JsExportNode*)node)->declaration);
        return;
    }
    if (node->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* declaration =
            (JsVariableDeclarationNode*)node;
        if (declaration->kind != JS_VAR_VAR) direct_define_variable(tp,
            declaration);
        return;
    }
    if (node->node_type == JS_AST_NODE_IMPORT_DECLARATION) {
        direct_define_import_bindings(tp, (JsImportNode*)node);
        return;
    }
    if (node->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
        direct_define_function(tp, (JsFunctionNode*)node,
            tp->current_scope && tp->current_scope->kind == SCOPE_KIND_BLOCK
                ? JS_SCOPE_BLOCK : JS_SCOPE_FUNCTION);
        return;
    }
    if (node->node_type == JS_AST_NODE_CLASS_DECLARATION) {
        JsClassNode* class_node = (JsClassNode*)node;
        if (class_node->name) {
            class_node->outer_entry = js_scope_define(tp, class_node->name,
                (JsAstNode*)class_node, JS_VAR_LET);
        }
    }
}

static void direct_predeclare_scope(JsTranspiler* tp, JsAstNode* node) {
    if (!node) return;
    // Function declarations are visible throughout their containing scope,
    // including while earlier sibling initializers are being constructed.
    for (JsAstNode* item = node; item; item = item->next) {
        JsAstNode* declaration = item;
        if (item->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
            declaration = ((JsExportNode*)item)->declaration;
        }
        if (declaration && declaration->node_type ==
                JS_AST_NODE_FUNCTION_DECLARATION) {
            direct_predeclare_one(tp, item);
        }
    }
    for (JsAstNode* item = node; item; item = item->next) {
        JsAstNode* declaration = item;
        if (item->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
            declaration = ((JsExportNode*)item)->declaration;
        }
        if (declaration && declaration->node_type ==
                JS_AST_NODE_FUNCTION_DECLARATION) continue;
        direct_predeclare_one(tp, item);
    }
}

static void direct_predeclare_var_node(JsTranspiler* tp, JsAstNode* node);

static void direct_scan_var_child(JsAstNode* child, void* opaque) {
    // Child visitation already enumerates a sibling list; process only the
    // callback's node so the remaining suffix is not recursively rescanned.
    direct_predeclare_var_node((JsTranspiler*)opaque, child);
}

static void direct_predeclare_var_node(JsTranspiler* tp, JsAstNode* node) {
    if (!tp || !node) return;
    // Nested callable/class bodies have their own var scope.
    if (direct_is_function(node) ||
            node->node_type == JS_AST_NODE_METHOD_DEFINITION ||
            direct_is_class(node)) return;
    if (node->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* declaration =
            (JsVariableDeclarationNode*)node;
        if (declaration->kind == JS_VAR_VAR) {
            direct_define_variable(tp, declaration);
        }
    } else {
        js_ast_visit_children(node, direct_scan_var_child, tp);
    }
}

static void direct_predeclare_vars(JsTranspiler* tp, JsAstNode* node) {
    if (!tp || !node) return;
    for (JsAstNode* item = node; item; item = item->next) {
        direct_predeclare_var_node(tp, item);
    }
}

static void direct_walk_function(JsTranspiler* tp, JsFunctionNode* function,
        bool method) {
    if (!tp || !function) return;
    JsScope* parent = tp->current_scope;
    JsScope* name_scope = NULL;
    if (!method && function->node_type == JS_AST_NODE_FUNCTION_EXPRESSION &&
            function->name) {
        // A named function expression resolves its name through an immutable
        // environment outside the ordinary function environment.
        name_scope = js_scope_create(tp, JS_SCOPE_BLOCK, parent);
        if (!name_scope) return;
        name_scope->is_function_name_scope = true;
        js_scope_push(tp, name_scope);
        NameEntry* self = js_scope_define_in_scope(tp, name_scope,
            function->name, (JsAstNode*)function, JS_VAR_CONST);
        if (!self) {
            js_scope_pop(tp);
            return;
        }
        self->is_mutable = false;
        self->is_function_name_binding = true;
        function->entry = self;
        parent = name_scope;
    }
        JsScope* scope = js_scope_create(tp, JS_SCOPE_FUNCTION, parent);
    if (!scope) {
        if (name_scope) js_scope_pop(tp);
        return;
    }
    scope->strict = parent ? parent->strict : tp->strict_mode;
    if (function->has_use_strict_directive) scope->strict = true;
    function->vars = scope;
    js_scope_push(tp, scope);

    // Parameter bindings exist while every default initializer is evaluated;
    // installing them first preserves the parameter TDZ over an outer name
    // with the same spelling and over later parameters.
    for (JsAstNode* parameter = (JsAstNode*)function->params; parameter;
            parameter = parameter->next) {
        direct_bind_pattern(tp, parameter, JS_VAR_VAR, NULL, true, false);
    }
    for (JsAstNode* parameter = (JsAstNode*)function->params; parameter;
            parameter = parameter->next) {
        // Defaults are evaluated in the parameter environment, before the
        // function body is entered.
        direct_walk_pattern_defaults(tp, parameter);
    }
    direct_predeclare_vars(tp, function->body);
    if (function->body && function->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        direct_walk_block(tp, (JsBlockNode*)function->body, JS_SCOPE_BLOCK, true);
    } else {
        direct_walk_node(tp, (JsAstNode*)function->body);
    }
    js_scope_pop(tp);
    if (name_scope) js_scope_pop(tp);
}

static void direct_walk_block(JsTranspiler* tp, JsBlockNode* block,
        JsScopeType scope_type, bool is_function_body) {
    if (!tp || !block) return;
    JsScope* scope = js_scope_create(tp, scope_type, tp->current_scope);
    if (!scope) return;
    scope->is_function_body = is_function_body;
    block->vars = scope;
    js_scope_push(tp, scope);
    direct_predeclare_scope(tp, block->statements);
    direct_walk_list(tp, block->statements);
    js_scope_pop(tp);
}

static void direct_walk_variable(JsTranspiler* tp,
        JsVariableDeclarationNode* declaration) {
    if (!declaration) return;
    for (JsAstNode* item = declaration->declarations; item;
            item = item->next) {
        if (item->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
        JsVariableDeclaratorNode* declarator =
            (JsVariableDeclaratorNode*)item;
        JsAstNode* owner = declarator->id &&
            declarator->id->node_type == JS_AST_NODE_IDENTIFIER ? item : NULL;
        direct_bind_pattern(tp, declarator->id,
            (JsVarKind)declaration->kind, owner, false, false);
        direct_walk_node(tp, declarator->init);
        direct_walk_pattern_defaults(tp, declarator->id);
        // The initializer is rebuilt after bindings are attached, so refresh
        // the declarator type using the same bottom-up result as the generic
        // declarator walk.
        declarator->type = declarator->init ? declarator->init->type
            : &TYPE_NULL;
    }
}

static void direct_walk_if_branch(JsTranspiler* tp, JsIfNode* conditional,
        JsAstNode* branch, NameScope** scope_out) {
    if (!branch) {
        direct_walk_node(tp, branch);
        return;
    }
    JsScope* scope = js_scope_create(tp, JS_SCOPE_BLOCK, tp->current_scope);
    if (!scope) return;
    if (scope_out) *scope_out = scope;
    js_scope_push(tp, scope);
    direct_predeclare_one(tp, branch);
    direct_walk_node(tp, branch);
    js_scope_pop(tp);
    (void)conditional;
}

static void direct_walk_for(JsTranspiler* tp, JsForNode* loop) {
    JsScope* scope = js_scope_create(tp, JS_SCOPE_BLOCK, tp->current_scope);
    if (!scope) return;
    loop->vars = scope;
    js_scope_push(tp, scope);
    direct_predeclare_one(tp, loop->init);
    direct_walk_node(tp, loop->init);
    direct_walk_node(tp, loop->test);
    direct_walk_node(tp, loop->update);
    direct_walk_node(tp, loop->body);
    js_scope_pop(tp);
}

static void direct_walk_for_of(JsTranspiler* tp, JsForOfNode* loop) {
    JsScope* scope = js_scope_create(tp, JS_SCOPE_BLOCK, tp->current_scope);
    if (!scope) return;
    loop->vars = scope;
    js_scope_push(tp, scope);
    if (loop->left && loop->left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        direct_walk_node(tp, loop->left);
    } else if (loop->declares_binding) {
        direct_bind_pattern(tp, loop->left, (JsVarKind)loop->kind, NULL,
            false, loop->node_type == JS_AST_NODE_FOR_IN_STATEMENT);
        direct_walk_pattern_defaults(tp, loop->left);
    } else {
        direct_walk_node(tp, loop->left);
    }
    direct_walk_node(tp, loop->right);
    direct_walk_node(tp, loop->body);
    js_scope_pop(tp);
}

static void direct_walk_catch(JsTranspiler* tp, JsCatchNode* handler) {
    JsScope* scope = js_scope_create(tp, JS_SCOPE_BLOCK, tp->current_scope);
    if (!scope) return;
    // Annex B.3.5 allows the handler's simple BindingIdentifier to share its
    // var region; destructured catch parameters must reject that redeclaration.
    scope->allows_legacy_var_redeclaration = handler->param &&
        handler->param->node_type == JS_AST_NODE_IDENTIFIER;
    handler->vars = scope;
    js_scope_push(tp, scope);
    direct_bind_pattern(tp, handler->param, JS_VAR_LET, NULL, false, false);
    // Catch initializers execute in the catch environment, so resolve their
    // references after the parameter bindings are installed.
    direct_walk_pattern_defaults(tp, handler->param);
    direct_walk_node(tp, handler->body);
    js_scope_pop(tp);
}

static void direct_walk_switch(JsTranspiler* tp, JsSwitchNode* switched) {
    direct_walk_node(tp, (JsAstNode*)switched->discriminant);
    JsScope* scope = js_scope_create(tp, JS_SCOPE_BLOCK, tp->current_scope);
    if (!scope) return;
    scope->is_switch_scope = true;
    switched->vars = scope;
    js_scope_push(tp, scope);
    for (JsAstNode* item = switched->cases; item; item = item->next) {
        if (item->node_type != JS_AST_NODE_SWITCH_CASE) continue;
        JsSwitchCaseNode* case_node = (JsSwitchCaseNode*)item;
        direct_predeclare_scope(tp, case_node->consequent);
    }
    for (JsAstNode* item = switched->cases; item; item = item->next) {
        if (item->node_type != JS_AST_NODE_SWITCH_CASE) continue;
        JsSwitchCaseNode* case_node = (JsSwitchCaseNode*)item;
        direct_walk_node(tp, case_node->test);
        direct_walk_list(tp, case_node->consequent);
    }
    js_scope_pop(tp);
}

static void direct_walk_class(JsTranspiler* tp, JsClassNode* class_node) {
    JsScope* saved = tp->current_scope;
    Type* saved_class_type = class_node->type;
    bool class_expression = class_node->node_type == JS_AST_NODE_CLASS_EXPRESSION;
    if (class_node->name) {
        JsScope* scope = js_scope_create(tp, JS_SCOPE_BLOCK, saved);
        if (!scope) return;
        class_node->expression_scope = scope;
        js_scope_push(tp, scope);
        // Every named class has a private immutable name environment for its
        // heritage expression and methods, including class declarations.
        class_node->type = NULL;
        js_scope_define(tp, class_node->name, (JsAstNode*)class_node,
            JS_VAR_CONST);
        NameEntry* self = js_scope_lookup_current(tp, class_node->name);
        if (self) {
            self->is_mutable = false;
            class_node->entry = self;
        }
    }
    direct_walk_node(tp, class_node->superclass);
    if (class_node->body &&
            class_node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        // class bodies do not contribute an executable lexical block; method
        // scopes are children of the class-expression scope when one exists.
        direct_walk_list(tp, ((JsBlockNode*)class_node->body)->statements);
    }
    if (tp->current_scope != saved) js_scope_pop(tp);
    if (class_expression) class_node->type = saved_class_type;
}

static void direct_walk_property(JsTranspiler* tp, JsPropertyNode* property) {
    if (!property) return;
    if (property->computed) direct_walk_node(tp, property->key);
    else if (property->key &&
            property->key->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* key = (JsIdentifierNode*)property->key;
        if (property->shorthand) {
            direct_walk_node(tp, property->value);
        } else if (property->method || property->is_getter ||
                property->is_setter) {
            key->entry = NULL;
            key->type = NULL;
        } else {
            key->entry = NULL;
            key->type = &TYPE_ANY;
        }
    } else {
        direct_walk_node(tp, property->key);
    }
    if (!property->shorthand) direct_walk_node(tp, property->value);
}

static void direct_walk_node(JsTranspiler* tp, JsAstNode* node) {
    if (!tp || !node) return;
    switch ((int)node->node_type) {
    case JS_AST_NODE_PROGRAM: {
        JsProgramNode* program = (JsProgramNode*)node;
        direct_predeclare_vars(tp, program->body);
        direct_predeclare_scope(tp, program->body);
        direct_walk_list(tp, program->body);
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT:
        direct_walk_block(tp, (JsBlockNode*)node, JS_SCOPE_BLOCK, false);
        break;
    case JS_AST_NODE_IDENTIFIER:
        direct_set_identifier(tp, (JsIdentifierNode*)node,
            js_scope_lookup(tp, ((JsIdentifierNode*)node)->name));
        break;
    case JS_AST_NODE_VARIABLE_DECLARATION:
        direct_walk_variable(tp, (JsVariableDeclarationNode*)node);
        break;
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* declarator =
            (JsVariableDeclaratorNode*)node;
        direct_walk_node(tp, declarator->init);
        declarator->type = declarator->init ? declarator->init->type
            : &TYPE_NULL;
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* binary = (JsBinaryNode*)node;
        direct_walk_node(tp, binary->left);
        direct_walk_node(tp, binary->right);
        refresh_js_binary_type(tp, (JsBinaryNode*)node);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* conditional = (JsConditionalNode*)node;
        direct_walk_node(tp, conditional->test);
        direct_walk_node(tp, conditional->consequent);
        direct_walk_node(tp, conditional->alternate);
        refresh_js_conditional_type(tp, conditional);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* assignment = (JsAssignmentNode*)node;
        direct_walk_node(tp, assignment->left);
        direct_walk_node(tp, assignment->right);
        refresh_js_assignment_type(assignment);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* statement =
            (JsExpressionStatementNode*)node;
        direct_walk_node(tp, statement->expression);
        if (statement->type != &TYPE_NULL) {
            statement->type = statement->expression &&
                    statement->expression->type
                ? statement->expression->type : &TYPE_NULL;
        }
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* result = (JsReturnNode*)node;
        direct_walk_node(tp, result->argument);
        result->type = result->argument ? result->argument->type : &TYPE_NULL;
        break;
    }
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
        direct_walk_function(tp, (JsFunctionNode*)node, false);
        break;
    case JS_AST_NODE_METHOD_DEFINITION: {
        JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)node;
        if (method->computed) direct_walk_node(tp, method->key);
        else if (method->key && method->key->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* key = (JsIdentifierNode*)method->key;
            key->entry = NULL;
            key->type = NULL;
        }
        direct_walk_function(tp, (JsFunctionNode*)method, true);
        break;
    }
    case JS_AST_NODE_STATIC_BLOCK: {
        JsStaticBlockNode* static_block = (JsStaticBlockNode*)node;
        if (static_block->body && static_block->body->node_type ==
                JS_AST_NODE_BLOCK_STATEMENT) {
            // A static block has a fresh function-like var environment; its
            // var declarations must not escape to the class or script scope.
            direct_walk_block(tp, (JsBlockNode*)static_block->body,
                JS_SCOPE_FUNCTION, false);
        } else {
            direct_walk_node(tp, static_block->body);
        }
        break;
    }
    case JS_AST_NODE_CLASS_DECLARATION:
    case JS_AST_NODE_CLASS_EXPRESSION:
        direct_walk_class(tp, (JsClassNode*)node);
        break;
    case JS_AST_NODE_PROPERTY:
        direct_walk_property(tp, (JsPropertyNode*)node);
        break;
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* member = (JsMemberNode*)node;
        direct_walk_node(tp, member->object);
        if (member->computed) direct_walk_node(tp, member->property);
        else if (member->property &&
                member->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* property = (JsIdentifierNode*)member->property;
            property->entry = NULL;
            property->type = &TYPE_ANY;
        }
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
    case JS_AST_NODE_ARRAY_PATTERN:
    case JS_AST_NODE_OBJECT_PATTERN:
        direct_walk_assignment_pattern(tp, node);
        break;
    case JS_AST_NODE_PARAMETER:
        direct_bind_pattern(tp, node, JS_VAR_VAR, NULL, true, false);
        direct_walk_pattern_defaults(tp, node);
        break;
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* conditional = (JsIfNode*)node;
        direct_walk_node(tp, conditional->test);
        conditional->consequent_vars = NULL;
        conditional->alternate_vars = NULL;
        direct_walk_if_branch(tp, conditional, conditional->consequent,
            &conditional->consequent_vars);
        direct_walk_if_branch(tp, conditional, conditional->alternate,
            &conditional->alternate_vars);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT:
        direct_walk_for(tp, (JsForNode*)node);
        break;
    case JS_AST_NODE_FOR_IN_STATEMENT:
    case JS_AST_NODE_FOR_OF_STATEMENT:
        direct_walk_for_of(tp, (JsForOfNode*)node);
        break;
    case JS_AST_NODE_CATCH_CLAUSE:
        direct_walk_catch(tp, (JsCatchNode*)node);
        break;
    case JS_AST_NODE_SWITCH_STATEMENT:
        direct_walk_switch(tp, (JsSwitchNode*)node);
        break;
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* tried = (JsTryNode*)node;
        direct_walk_node(tp, tried->block);
        direct_walk_node(tp, tried->handler);
        direct_walk_node(tp, tried->finalizer);
        break;
    }
    case JS_AST_NODE_IMPORT_DECLARATION: {
        direct_define_import_bindings(tp, (JsImportNode*)node);
        break;
    }
    case JS_AST_NODE_EXPORT_SPECIFIER:
    case JS_AST_NODE_IMPORT_SPECIFIER:
        break;
    case JS_AST_NODE_EXPORT_DECLARATION: {
        JsExportNode* export_node = (JsExportNode*)node;
        direct_walk_node(tp, export_node->declaration);
        // Export specifiers retain the resolved declaration edge because MIR
        // publication must not reconstruct a local binding from its spelling.
        for (JsAstNode* spec = export_node->specifiers; spec;
                spec = spec->next) {
            if (spec->node_type == JS_AST_NODE_EXPORT_SPECIFIER) {
                JsExportSpecifierNode* export_spec =
                    (JsExportSpecifierNode*)spec;
                export_spec->local_entry = js_scope_lookup(tp,
                    export_spec->local_name);
            }
        }
        break;
    }
    case TS_AST_NODE_PARAMETER: {
        TsParameterNode* parameter = (TsParameterNode*)node;
        direct_bind_pattern(tp, parameter->pattern, JS_VAR_VAR, NULL, true,
            false);
        direct_walk_node(tp, parameter->default_value);
        break;
    }
    default:
        js_ast_visit_children(node, direct_walk_child, tp);
        break;
    }
}

static void direct_walk_list(JsTranspiler* tp, JsAstNode* node) {
    for (JsAstNode* item = node; item; item = item->next) {
        direct_walk_node(tp, item);
    }
}

}  // namespace

bool js_rebuild_direct_scope_graph(JsTranspiler* tp, JsAstNode* ast) {
    if (!tp || !ast || ast->node_type != JS_AST_NODE_PROGRAM) return false;
    JsScope* global = js_scope_create(tp,
        tp->is_module ? JS_SCOPE_MODULE : JS_SCOPE_GLOBAL, NULL);
    if (!global) return false;
    global->strict = tp->strict_mode;
    tp->global_scope = global;
    tp->current_scope = global;
    ((JsProgramNode*)ast)->global_vars = global;
    direct_walk_node(tp, ast);
    tp->current_scope = global;
    return true;
}
