#include "js_transpiler.hpp"
#include "js_c_ast_helpers.hpp"
#include "../ts/ts_ast.hpp"
#include "../../lib/arraylist.h"
#include "../../lib/mempool.h"

struct JsDirectPredeclaredFact {
    String* name;
    SourceSpan span;
};

struct JsDirectScopeState {
    ArrayList* reference_predeclared;
    ArrayList* parameter_entries;
    bool suppress_pattern_keys;
    bool allow_for_head_keys;
    bool preserve_parameter_types;
};

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
static void direct_prepare_parameter_type(JsTranspiler* tp,
        JsAstNode* pattern, bool preserve_outer_type);
static void direct_clear_class_self_parameter(JsAstNode* pattern,
        String* class_name);

static bool direct_same_name(String* lhs, String* rhs) {
    return lhs && rhs && lhs->len == rhs->len &&
        memcmp(lhs->chars, rhs->chars, lhs->len) == 0;
}

static int direct_compare_predeclared_facts(ArrayListValue lhs_value,
        ArrayListValue rhs_value) {
    const JsDirectPredeclaredFact* lhs =
        (const JsDirectPredeclaredFact*)lhs_value;
    const JsDirectPredeclaredFact* rhs =
        (const JsDirectPredeclaredFact*)rhs_value;
    const char* lhs_name = lhs && lhs->name ? lhs->name->chars : "";
    const char* rhs_name = rhs && rhs->name ? rhs->name->chars : "";
    int name_order = strcmp(lhs_name, rhs_name);
    if (name_order != 0) return name_order;
    SourceSpan lhs_span = lhs ? lhs->span : (SourceSpan){0, 0};
    SourceSpan rhs_span = rhs ? rhs->span : (SourceSpan){0, 0};
    if (lhs_span.start_byte != rhs_span.start_byte) {
        return lhs_span.start_byte < rhs_span.start_byte ? -1 : 1;
    }
    if (lhs_span.end_byte != rhs_span.end_byte) {
        return lhs_span.end_byte < rhs_span.end_byte ? -1 : 1;
    }
    return 0;
}

static bool direct_span_contains(SourceSpan outer, SourceSpan inner) {
    return outer.start_byte <= inner.start_byte &&
        outer.end_byte >= inner.end_byte;
}

static void direct_record_predeclared(JsTranspiler* tp, String* name,
        SourceSpan span) {
    if (!tp || !name || !tp->direct_scope_state ||
            !tp->direct_scope_state->reference_predeclared) return;
    JsDirectPredeclaredFact* fact = (JsDirectPredeclaredFact*)pool_alloc(
        tp->pool, sizeof(JsDirectPredeclaredFact));
    if (!fact) return;
    fact->name = name;
    fact->span = span;
    if (!arraylist_append(tp->direct_scope_state->reference_predeclared,
            fact)) return;
}

static void direct_collect_predeclared_pattern(JsTranspiler* tp,
        JsAstNode* pattern) {
    if (!tp || !pattern) return;
    if (pattern->node_type == (JsAstNodeType)TS_AST_NODE_PARAMETER) {
        direct_collect_predeclared_pattern(tp,
            ((TsParameterNode*)pattern)->pattern);
        return;
    }
    switch (pattern->node_type) {
    case JS_AST_NODE_IDENTIFIER:
        direct_record_predeclared(tp, ((JsIdentifierNode*)pattern)->name,
            pattern->source_span);
        break;
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        direct_collect_predeclared_pattern(tp,
            ((JsAssignmentPatternNode*)pattern)->left);
        break;
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        direct_collect_predeclared_pattern(tp,
            ((JsSpreadElementNode*)pattern)->argument);
        break;
    case JS_AST_NODE_ARRAY_PATTERN:
        for (JsAstNode* item = ((JsArrayPatternNode*)pattern)->elements;
                item; item = item->next) {
            direct_collect_predeclared_pattern(tp, item);
        }
        break;
    case JS_AST_NODE_OBJECT_PATTERN:
        for (JsAstNode* item = ((JsObjectPatternNode*)pattern)->properties;
                item; item = item->next) {
            if (item->node_type != JS_AST_NODE_PROPERTY) {
                direct_collect_predeclared_pattern(tp, item);
                continue;
            }
            JsPropertyNode* property = (JsPropertyNode*)item;
            JsAstNode* value = property->value;
            // Tree-sitter does not predeclare `{name = default}` through its
            // object_assignment_pattern node; colon patterns do recurse.
            if (value && value->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN &&
                    value->source_span.start_byte == item->source_span.start_byte) {
                continue;
            }
            direct_collect_predeclared_pattern(tp, value);
        }
        break;
    default:
        break;
    }
}

static void direct_collect_predeclared_child(JsAstNode* child, void* opaque);

static void direct_collect_predeclared_node(JsTranspiler* tp,
        JsAstNode* node) {
    if (!tp || !node) return;
    switch (node->node_type) {
        case JS_AST_NODE_VARIABLE_DECLARATION: {
            JsVariableDeclarationNode* declaration =
                (JsVariableDeclarationNode*)node;
            for (JsAstNode* declaration_item = declaration->declarations;
                    declaration_item; declaration_item = declaration_item->next) {
                if (declaration_item->node_type ==
                        JS_AST_NODE_VARIABLE_DECLARATOR) {
                    direct_collect_predeclared_pattern(tp,
                        ((JsVariableDeclaratorNode*)declaration_item)->id);
                }
            }
            break;
        }
        case JS_AST_NODE_FUNCTION_DECLARATION:
        case JS_AST_NODE_FUNCTION_EXPRESSION:
        case JS_AST_NODE_ARROW_FUNCTION: {
            JsFunctionNode* function = (JsFunctionNode*)node;
            if (function->name) direct_record_predeclared(tp, function->name,
                function->source_span);
            js_ast_visit_children(node, direct_collect_predeclared_child, tp);
            break;
        }
        case JS_AST_NODE_CLASS_DECLARATION:
        case JS_AST_NODE_CLASS_EXPRESSION: {
            JsClassNode* class_node = (JsClassNode*)node;
            if (class_node->name) direct_record_predeclared(tp,
                class_node->name, class_node->source_span);
            js_ast_visit_children(node, direct_collect_predeclared_child, tp);
            break;
        }
        case JS_AST_NODE_FOR_STATEMENT: {
            JsForNode* loop = (JsForNode*)node;
            direct_collect_predeclared_node(tp, loop->init);
            direct_collect_predeclared_node(tp, loop->test);
            direct_collect_predeclared_node(tp, loop->update);
            direct_collect_predeclared_node(tp, loop->body);
            break;
        }
        case JS_AST_NODE_FOR_IN_STATEMENT:
        case JS_AST_NODE_FOR_OF_STATEMENT: {
            JsForOfNode* loop = (JsForOfNode*)node;
            if (loop->left && loop->left->node_type ==
                    JS_AST_NODE_VARIABLE_DECLARATION) {
                direct_collect_predeclared_node(tp, loop->left);
            } else if (loop->declares_binding) {
                direct_collect_predeclared_pattern(tp, loop->left);
            }
            direct_collect_predeclared_node(tp, loop->right);
            direct_collect_predeclared_node(tp, loop->body);
            break;
        }
        default:
            js_ast_visit_children(node, direct_collect_predeclared_child, tp);
            break;
    }
}

static void direct_collect_predeclared_child(JsAstNode* child, void* opaque) {
    direct_collect_predeclared_node((JsTranspiler*)opaque, child);
}

static bool direct_reference_key_is_visible(JsTranspiler* tp,
        NameEntry* entry, SourceSpan use_span,
        bool allow_declarator_initializer) {
    if (!tp || !entry || !entry->node || !tp->direct_scope_state ||
            !tp->direct_scope_state->reference_predeclared) return false;
    SourceSpan entry_span = entry->node->source_span;
    if (((entry->node->node_type == JS_AST_NODE_VARIABLE_DECLARATOR &&
            !allow_declarator_initializer) ||
            (entry->node->node_type == JS_AST_NODE_IDENTIFIER &&
                entry->is_lexical)) &&
            direct_span_contains(entry_span, use_span)) {
        // The reference adapter builds a declarator initializer before its
        // binding is published; a pattern label at the binding site is also
        // a property name, not a read of that binding.
        return false;
    }
    // Lexical declarations are installed for the whole block before its
    // initializers are built, including nested function blocks.
    if (entry->is_lexical) return true;
    if (entry->node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION &&
            direct_span_contains(entry_span, use_span)) {
        // A named function expression owns a self-binding throughout its
        // body, independent of the enclosing scope's predeclaration facts.
        return true;
    }
    ArrayList* facts = tp->direct_scope_state->reference_predeclared;
    int low = 0;
    int high = arraylist_length(facts);
    while (low < high) {
        int middle = low + (high - low) / 2;
        JsDirectPredeclaredFact* fact = (JsDirectPredeclaredFact*)
            arraylist_get(facts, middle);
        const char* fact_name = fact && fact->name ? fact->name->chars : "";
        const char* entry_name = entry->name ? entry->name->chars : "";
        if (strcmp(fact_name, entry_name) < 0) low = middle + 1;
        else high = middle;
    }
    for (int i = low; i < arraylist_length(facts); i++) {
        JsDirectPredeclaredFact* fact = (JsDirectPredeclaredFact*)
            arraylist_get(facts, i);
        if (!fact || !direct_same_name(fact->name, entry->name)) break;
        if (direct_span_contains(entry_span, fact->span)) return true;
    }
    // Parameters and loop-head bindings are installed while the enclosing
    // construct is entered rather than by the top-level predeclaration walk.
    bool is_parameter = false;
    ArrayList* parameters = tp->direct_scope_state->parameter_entries;
    for (int parameter_index = 0; parameters &&
            parameter_index < arraylist_length(parameters); parameter_index++) {
        if (arraylist_get(parameters, parameter_index) == entry) {
            is_parameter = true;
            break;
        }
    }
    if (!entry->is_lexical && entry->node->node_type == JS_AST_NODE_IDENTIFIER &&
            (is_parameter || entry->is_var_param || entry->is_for_in_head) &&
            entry_span.end_byte <= use_span.start_byte) return true;
    if (entry->node->node_type == JS_AST_NODE_VARIABLE_DECLARATOR &&
            entry_span.end_byte <= use_span.start_byte) return true;
    return false;
}

static void direct_record_parameter_entries(JsTranspiler* tp,
        JsAstNode* pattern) {
    if (!tp || !pattern || !tp->direct_scope_state ||
            !tp->direct_scope_state->parameter_entries) return;
    if (pattern->node_type == (JsAstNodeType)TS_AST_NODE_PARAMETER) {
        direct_record_parameter_entries(tp,
            ((TsParameterNode*)pattern)->pattern);
        return;
    }
    switch (pattern->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        NameEntry* entry = js_scope_lookup_current(tp,
            ((JsIdentifierNode*)pattern)->name);
        if (entry) {
            entry->is_parameter = true;
            arraylist_append(tp->direct_scope_state->parameter_entries, entry);
        }
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        direct_record_parameter_entries(tp,
            ((JsAssignmentPatternNode*)pattern)->left);
        break;
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        direct_record_parameter_entries(tp,
            ((JsSpreadElementNode*)pattern)->argument);
        break;
    case JS_AST_NODE_ARRAY_PATTERN:
        for (JsAstNode* item = ((JsArrayPatternNode*)pattern)->elements;
                item; item = item->next) {
            direct_record_parameter_entries(tp, item);
        }
        break;
    case JS_AST_NODE_OBJECT_PATTERN:
        for (JsAstNode* item = ((JsObjectPatternNode*)pattern)->properties;
                item; item = item->next) {
            direct_record_parameter_entries(tp,
                item->node_type == JS_AST_NODE_PROPERTY
                    ? ((JsPropertyNode*)item)->value : item);
        }
        break;
    default:
        break;
    }
}

static void direct_walk_child(JsAstNode* child, void* opaque) {
    direct_walk_node((JsTranspiler*)opaque, child);
}

static void direct_set_identifier(JsTranspiler* tp, JsIdentifierNode* id,
        NameEntry* entry) {
    if (!id) return;
    id->entry = entry;
    // Preserve the bottom-up identifier type when the direct parser already
    // observed the same binding state as the reference builder. Scope repair
    // must attach the entry, but replacing an earlier open type with a later
    // declaration's initializer would make source-order facts diverge.
    bool declaration_precedes_use = entry && entry->node &&
        entry->node->source_span.start_byte <= id->source_span.start_byte;
    bool use_inside_declarator = entry && entry->node &&
        entry->node->node_type == JS_AST_NODE_VARIABLE_DECLARATOR &&
        direct_span_contains(entry->node->source_span,
            id->source_span);
    if (entry && entry->node && use_inside_declarator) {
        // A hoisted declarator is still unresolved throughout its own
        // initializer; its placeholder is not the initializer's final type.
        id->type = &TYPE_ANY;
        return;
    }
    if (entry && entry->node && !declaration_precedes_use &&
            entry->node->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
        // Function declarations are hoisted with their callable value, so a
        // pre-declaration reference keeps the function type.
        id->type = entry->node->type;
        return;
    }
    if (entry && entry->node && !declaration_precedes_use &&
            entry->node->node_type == JS_AST_NODE_CLASS_DECLARATION) {
        // Class declarations retain their constructor type in indexed facts
        // even when a reference appears before the TDZ declaration.
        id->type = entry->node->type;
        return;
    }
    if (entry && entry->node && !declaration_precedes_use &&
            entry->node->node_type != JS_AST_NODE_IDENTIFIER) {
        // Bottom-up direct reductions can see a later declaration's concrete
        // type; the reference adapter saw an unresolved name at this point.
        id->type = &TYPE_ANY;
        return;
    }
    if (entry && entry->node &&
            (entry->node->node_type == JS_AST_NODE_IDENTIFIER ||
             declaration_precedes_use)) {
        id->type = entry->node->type;
    } else {
        // Parser-time reductions may have resolved this identifier against a
        // scope that is discarded by the post-build graph. Clear that stale
        // type when the rebuilt graph has no binding.
        id->type = js_set_type_any(tp, ANY_OPEN_PARAM);
    }
}

static void direct_bind_pattern(JsTranspiler* tp, JsAstNode* pattern,
        JsVarKind kind, JsAstNode* owner) {
    if (!tp || !pattern) return;
    if (pattern->node_type == (JsAstNodeType)TS_AST_NODE_PARAMETER) {
        direct_bind_pattern(tp, ((TsParameterNode*)pattern)->pattern, kind,
            NULL);
        return;
    }
    switch (pattern->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)pattern;
        NameEntry* entry = js_scope_lookup_current(tp, id->name);
        if (!entry) entry = js_scope_define(tp, id->name,
            owner ? owner : pattern, kind);
        id->entry = entry;
        // Binding identifiers are not reads. Rebuild their open parameter
        // state after the parser-time scope has been discarded; otherwise a
        // same-named declaration from an unrelated enclosing construct leaks
        // into the declaration's static type.
        if (!tp->direct_scope_state ||
                !tp->direct_scope_state->preserve_parameter_types) {
            id->type = js_set_type_any(tp, ANY_OPEN_PARAM);
        }
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        direct_bind_pattern(tp, ((JsAssignmentPatternNode*)pattern)->left,
            kind, NULL);
        break;
    case JS_AST_NODE_REST_ELEMENT:
        direct_bind_pattern(tp, ((JsSpreadElementNode*)pattern)->argument,
            kind, NULL);
        break;
    case JS_AST_NODE_REST_PROPERTY:
        // Object-rest arguments are binding patterns, even though their
        // runtime collection is handled by the enclosing object pattern.
        direct_bind_pattern(tp, ((JsSpreadElementNode*)pattern)->argument,
            kind, NULL);
        break;
    case JS_AST_NODE_SPREAD_ELEMENT:
        break;
    case JS_AST_NODE_ARRAY_PATTERN:
        for (JsAstNode* item = ((JsArrayPatternNode*)pattern)->elements;
                item; item = item->next) {
            direct_bind_pattern(tp, item, kind, NULL);
        }
        break;
    case JS_AST_NODE_OBJECT_PATTERN: {
        for (JsAstNode* item = ((JsObjectPatternNode*)pattern)->properties;
                item; item = item->next) {
            if (item->node_type == JS_AST_NODE_PROPERTY) {
                JsPropertyNode* property = (JsPropertyNode*)item;
                if (property->computed) direct_walk_node(tp, property->key);
                else if (property->key && property->key->node_type ==
                        JS_AST_NODE_IDENTIFIER) {
                    // Preserve the reference adapter's construction-time
                    // lookup for colliding labels, while leaving ordinary
                    // property labels unresolved when no prior binding was
                    // visible.
                    JsIdentifierNode* key = (JsIdentifierNode*)property->key;
                    bool assignment_label = property->value &&
                        property->value->node_type ==
                            JS_AST_NODE_ASSIGNMENT_PATTERN &&
                        property->value->source_span.start_byte ==
                            item->source_span.start_byte;
                    NameEntry* entry = assignment_label ? NULL
                        : js_scope_lookup(tp, key->name);
                    bool visible = entry && direct_reference_key_is_visible(
                        tp, entry, key->source_span, false);
                    if (!visible && entry && entry->node &&
                            entry->node->node_type ==
                                JS_AST_NODE_VARIABLE_DECLARATOR &&
                            entry->node->source_span.end_byte <=
                                key->source_span.start_byte) {
                        visible = true;
                    }
                    if (!assignment_label &&
                            (!tp->direct_scope_state->suppress_pattern_keys ||
                                visible) && entry) {
                        direct_set_identifier(tp, key, entry);
                    } else {
                        key->entry = NULL;
                        key->type = &TYPE_ANY;
                    }
                }
                direct_bind_pattern(tp, property->value, kind, NULL);
            } else {
                direct_bind_pattern(tp, item, kind, NULL);
            }
        }
        break;
    }
    default:
        break;
    }
}

static void direct_walk_pattern_defaults(JsTranspiler* tp, JsAstNode* pattern) {
    if (!pattern) return;
    if (pattern->node_type == (JsAstNodeType)TS_AST_NODE_PARAMETER) {
        TsParameterNode* parameter = (TsParameterNode*)pattern;
        direct_walk_pattern_defaults(tp, parameter->pattern);
        direct_walk_node(tp, parameter->default_value);
        return;
    }
    switch (pattern->node_type) {
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        direct_walk_pattern_defaults(tp,
            ((JsAssignmentPatternNode*)pattern)->left);
        direct_walk_node(tp, ((JsAssignmentPatternNode*)pattern)->right);
        break;
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        direct_walk_pattern_defaults(tp,
            ((JsSpreadElementNode*)pattern)->argument);
        break;
    case JS_AST_NODE_ARRAY_PATTERN:
        for (JsAstNode* item = ((JsArrayPatternNode*)pattern)->elements;
                item; item = item->next) {
            direct_walk_pattern_defaults(tp, item);
        }
        break;
    case JS_AST_NODE_OBJECT_PATTERN:
        for (JsAstNode* item = ((JsObjectPatternNode*)pattern)->properties;
                item; item = item->next) {
            if (item->node_type == JS_AST_NODE_PROPERTY) {
                JsPropertyNode* property = (JsPropertyNode*)item;
                direct_walk_pattern_defaults(tp, property->value);
            } else {
                direct_walk_pattern_defaults(tp, item);
            }
        }
        break;
    default:
        break;
    }
}

static void direct_walk_assignment_pattern(JsTranspiler* tp,
        JsAstNode* pattern) {
    if (!tp || !pattern) return;
    switch (pattern->node_type) {
    case JS_AST_NODE_IDENTIFIER:
        direct_set_identifier(tp, (JsIdentifierNode*)pattern,
            js_scope_lookup(tp, ((JsIdentifierNode*)pattern)->name));
        break;
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        direct_walk_assignment_pattern(tp,
            ((JsAssignmentPatternNode*)pattern)->left);
        direct_walk_node(tp, ((JsAssignmentPatternNode*)pattern)->right);
        break;
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        direct_walk_assignment_pattern(tp,
            ((JsSpreadElementNode*)pattern)->argument);
        break;
    case JS_AST_NODE_ARRAY_PATTERN:
        for (JsAstNode* item = ((JsArrayPatternNode*)pattern)->elements;
                item; item = item->next) {
            direct_walk_assignment_pattern(tp, item);
        }
        break;
    case JS_AST_NODE_OBJECT_PATTERN:
        for (JsAstNode* item = ((JsObjectPatternNode*)pattern)->properties;
                item; item = item->next) {
            if (item->node_type == JS_AST_NODE_PROPERTY) {
                JsPropertyNode* property = (JsPropertyNode*)item;
                if (property->computed) direct_walk_node(tp, property->key);
                else if (property->key && property->key->node_type ==
                        JS_AST_NODE_IDENTIFIER) {
                    // Assignment patterns use the same reference-adapter
                    // key lookup as declaration patterns; only visible
                    // collisions become indexed identifier facts.
                    JsIdentifierNode* key = (JsIdentifierNode*)property->key;
                    NameEntry* entry = js_scope_lookup(tp, key->name);
                    if (entry && direct_reference_key_is_visible(tp, entry,
                            key->source_span, false)) {
                        direct_set_identifier(tp, key, entry);
                    } else {
                        key->entry = NULL;
                        key->type = &TYPE_ANY;
                    }
                }
                direct_walk_assignment_pattern(tp, property->value);
            } else {
                direct_walk_assignment_pattern(tp, item);
            }
        }
        break;
    default:
        direct_walk_node(tp, pattern);
        break;
    }
}

static void direct_define_pattern(JsTranspiler* tp, JsAstNode* pattern,
        JsVarKind kind, JsAstNode* owner, JsAstNode* declarator_owner,
        bool rest_binding) {
    if (!tp || !pattern) return;
    if (pattern->node_type == (JsAstNodeType)TS_AST_NODE_PARAMETER) {
        direct_define_pattern(tp, ((TsParameterNode*)pattern)->pattern, kind,
            NULL, declarator_owner, rest_binding);
        return;
    }
    switch (pattern->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)pattern;
        JsAstNode* binding_node = owner ? owner : pattern;
        JsIdentifierNode* placeholder = NULL;
        if (rest_binding && declarator_owner) {
            placeholder = (JsIdentifierNode*)pool_alloc(tp->pool,
                sizeof(JsIdentifierNode));
            memset(placeholder, 0, sizeof(JsIdentifierNode));
            placeholder->node_type = JS_AST_NODE_IDENTIFIER;
            placeholder->source_span = declarator_owner->source_span;
            placeholder->name = id->name;
            placeholder->type = &TYPE_ANY;
            binding_node = (JsAstNode*)placeholder;
        }
        NameEntry* entry = js_scope_define(tp, id->name, binding_node, kind);
        if (rest_binding) id->entry = entry;
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        direct_define_pattern(tp, ((JsAssignmentPatternNode*)pattern)->left,
            kind, NULL, declarator_owner, rest_binding);
        break;
    case JS_AST_NODE_REST_ELEMENT:
        direct_define_pattern(tp, ((JsSpreadElementNode*)pattern)->argument,
            kind, NULL, declarator_owner, rest_binding);
        break;
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        direct_define_pattern(tp, ((JsSpreadElementNode*)pattern)->argument,
            kind, NULL, declarator_owner, true);
        break;
    case JS_AST_NODE_ARRAY_PATTERN:
        for (JsAstNode* item = ((JsArrayPatternNode*)pattern)->elements;
                item; item = item->next) {
            direct_define_pattern(tp, item, kind, NULL, declarator_owner,
                false);
        }
        break;
    case JS_AST_NODE_OBJECT_PATTERN:
        for (JsAstNode* item = ((JsObjectPatternNode*)pattern)->properties;
                item; item = item->next) {
            if (item->node_type == JS_AST_NODE_PROPERTY) {
                direct_define_pattern(tp, ((JsPropertyNode*)item)->value,
                    kind, NULL, declarator_owner, false);
            } else {
                direct_define_pattern(tp, item, kind, NULL, declarator_owner,
                    false);
            }
        }
        break;
    default:
        break;
    }
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
    if (outer && !outer->is_parameter) lexical->annex_b_outer_binding = outer;
}

static void direct_define_class(JsTranspiler* tp, JsClassNode* class_node) {
    if (!class_node || !class_node->name) return;
    if (!tp->strict_js) {
        // The TypeScript adapter retains a lexical class-name placeholder and
        // a value binding for the constructor in the outer scope.
        JsIdentifierNode* placeholder = (JsIdentifierNode*)pool_alloc(
            tp->pool, sizeof(JsIdentifierNode));
        if (placeholder) {
            memset(placeholder, 0, sizeof(JsIdentifierNode));
            placeholder->node_type = JS_AST_NODE_IDENTIFIER;
            placeholder->source_span = class_node->source_span;
            placeholder->name = class_node->name;
            placeholder->type = &TYPE_FUNC;
            js_scope_define(tp, class_node->name, (JsAstNode*)placeholder,
                JS_VAR_LET);
        }
        js_scope_define(tp, class_node->name, (JsAstNode*)class_node,
            JS_VAR_VAR);
        return;
    }
    js_scope_define(tp, class_node->name, (JsAstNode*)class_node, JS_VAR_LET);
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
    if (node->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
        direct_define_function(tp, (JsFunctionNode*)node,
            tp->current_scope && tp->current_scope->kind == SCOPE_KIND_BLOCK
                ? JS_SCOPE_BLOCK : JS_SCOPE_FUNCTION);
        return;
    }
    if (node->node_type == JS_AST_NODE_CLASS_DECLARATION) {
        direct_define_class(tp, (JsClassNode*)node);
    }
}

static void direct_predeclare_namespace_functions(JsTranspiler* tp,
        TsNamespaceDeclarationNode* namespace_node) {
    if (!tp || !namespace_node) return;
    for (int i = 0; i < namespace_node->body_count; i++) {
        JsAstNode* item = namespace_node->body[i];
        JsAstNode* declaration = item;
        if (item && item->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
            declaration = ((JsExportNode*)item)->declaration;
        }
        if (!declaration) continue;
        if (declaration->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            direct_predeclare_one(tp, item);
        } else if (declaration->node_type ==
                (JsAstNodeType)TS_AST_NODE_NAMESPACE_DECLARATION) {
            direct_predeclare_namespace_functions(tp,
                (TsNamespaceDeclarationNode*)declaration);
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
        } else if (declaration && declaration->node_type ==
                (JsAstNodeType)TS_AST_NODE_NAMESPACE_DECLARATION) {
            direct_predeclare_namespace_functions(tp,
                (TsNamespaceDeclarationNode*)declaration);
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

static void direct_prepare_parameter_type(JsTranspiler* tp,
        JsAstNode* pattern, bool preserve_outer_type) {
    if (!tp || !pattern) return;
    if (pattern->node_type == (JsAstNodeType)TS_AST_NODE_PARAMETER) {
        direct_prepare_parameter_type(tp,
            ((TsParameterNode*)pattern)->pattern, preserve_outer_type);
        return;
    }
    switch (pattern->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)pattern;
        NameEntry* entry = js_scope_lookup(tp, id->name);
        // The reference builder resolves a parameter against an already
        // visible outer binding, but a parameter nested in its own var
        // initializer is built before that declarator is published.
        id->type = js_set_type_any(tp, ANY_OPEN_PARAM);
        if (preserve_outer_type && entry && entry->node) {
            if (entry->node->node_type == JS_AST_NODE_CLASS_EXPRESSION) {
                id->type = NULL;
            } else if (entry->node->node_type ==
                    JS_AST_NODE_FUNCTION_DECLARATION) {
                // Function declarations are hoisted before parameter
                // expressions are constructed, regardless of source order.
                id->type = entry->node->type;
            } else if (entry->node->node_type !=
                    JS_AST_NODE_VARIABLE_DECLARATOR) {
                id->type = entry->node->type;
            } else if (entry->node->node_type ==
                    JS_AST_NODE_VARIABLE_DECLARATOR &&
                    entry->node->source_span.end_byte <=
                        id->source_span.start_byte) {
                id->type = entry->node->type;
            }
        }
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        direct_prepare_parameter_type(tp,
            ((JsAssignmentPatternNode*)pattern)->left, preserve_outer_type);
        break;
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        direct_prepare_parameter_type(tp,
            ((JsSpreadElementNode*)pattern)->argument, preserve_outer_type);
        break;
    case JS_AST_NODE_ARRAY_PATTERN:
        for (JsAstNode* item = ((JsArrayPatternNode*)pattern)->elements;
                item; item = item->next) {
            direct_prepare_parameter_type(tp, item, preserve_outer_type);
        }
        break;
    case JS_AST_NODE_OBJECT_PATTERN:
        for (JsAstNode* item = ((JsObjectPatternNode*)pattern)->properties;
                item; item = item->next) {
            if (item->node_type == JS_AST_NODE_PROPERTY) {
                direct_prepare_parameter_type(tp,
                    ((JsPropertyNode*)item)->value, preserve_outer_type);
            } else {
                direct_prepare_parameter_type(tp, item, preserve_outer_type);
            }
        }
        break;
    default:
        break;
    }
}

static bool direct_arrow_parameters_are_parenthesized(JsTranspiler* tp,
        JsFunctionNode* function) {
    if (!function || !function->is_arrow) return true;
    JsAstNode* first = (JsAstNode*)function->params;
    if (!first || !tp || !tp->source || first->source_span.start_byte == 0) {
        return false;
    }
    size_t end = first->source_span.end_byte;
    while (end < tp->source_length && (tp->source[end] == ' ' ||
            tp->source[end] == '\t' || tp->source[end] == '\n' ||
            tp->source[end] == '\r' || tp->source[end] == '\f' ||
            tp->source[end] == '\v')) end++;
    if (end + 1 < tp->source_length && tp->source[end] == '=' &&
            tp->source[end + 1] == '>') return false;
    size_t position = first->source_span.start_byte;
    while (position > 0) {
        char ch = tp->source[--position];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
                ch == '\f' || ch == '\v') continue;
        return ch == '(';
    }
    return false;
}

static void direct_clear_class_self_parameter(JsAstNode* pattern,
        String* class_name) {
    if (!pattern || !class_name) return;
    if (pattern->node_type == (JsAstNodeType)TS_AST_NODE_PARAMETER) {
        direct_clear_class_self_parameter(
            ((TsParameterNode*)pattern)->pattern, class_name);
        return;
    }
    switch (pattern->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)pattern;
        if (id->name && id->name->len == class_name->len &&
                memcmp(id->name->chars, class_name->chars,
                    class_name->len) == 0) id->type = NULL;
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        direct_clear_class_self_parameter(
            ((JsAssignmentPatternNode*)pattern)->left, class_name);
        break;
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        direct_clear_class_self_parameter(
            ((JsSpreadElementNode*)pattern)->argument, class_name);
        break;
    case JS_AST_NODE_ARRAY_PATTERN:
        for (JsAstNode* item = ((JsArrayPatternNode*)pattern)->elements;
                item; item = item->next) {
            direct_clear_class_self_parameter(item, class_name);
        }
        break;
    case JS_AST_NODE_OBJECT_PATTERN:
        for (JsAstNode* item = ((JsObjectPatternNode*)pattern)->properties;
                item; item = item->next) {
            direct_clear_class_self_parameter(
                item->node_type == JS_AST_NODE_PROPERTY
                    ? ((JsPropertyNode*)item)->value : item, class_name);
        }
        break;
    default:
        break;
    }
}

static void direct_walk_function(JsTranspiler* tp, JsFunctionNode* function,
        bool method) {
    if (!tp || !function) return;
    JsScope* parent = tp->current_scope;
    JsScope* name_scope = NULL;
    String* class_self_name = NULL;
    if (method && parent && parent->kind == SCOPE_KIND_BLOCK) {
        for (NameEntry* entry = parent->first; entry; entry = entry->next) {
            if (entry->node && entry->node->node_type ==
                    JS_AST_NODE_CLASS_EXPRESSION) {
                class_self_name = entry->name;
                break;
            }
        }
    }
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
        parent = name_scope;
    }
        JsScope* scope = js_scope_create(tp, JS_SCOPE_FUNCTION, parent);
    if (!scope) {
        if (name_scope) js_scope_pop(tp);
        return;
    }
    if (!method && function->node_type == JS_AST_NODE_FUNCTION_DECLARATION &&
            function->name) {
        // Duplicate function declarations replace the earlier hoisted value;
        // the predeclaration pass only installs the first binding carrier.
        NameEntry* declaration = js_scope_lookup_current(tp, function->name);
        if (declaration && !declaration->is_parameter) {
            declaration->node = (AstNode*)function;
        }
    }
    scope->strict = parent ? parent->strict : tp->strict_mode;
    if (function->has_use_strict_directive) scope->strict = true;
    function->vars = scope;
    js_scope_push(tp, scope);

    bool preserve_outer_type = !function->is_arrow ||
        direct_arrow_parameters_are_parenthesized(tp, function);
    // Parameter bindings exist while every default initializer is evaluated;
    // installing them first preserves the parameter TDZ over an outer name
    // with the same spelling and over later parameters.
    for (JsAstNode* parameter = (JsAstNode*)function->params; parameter;
            parameter = parameter->next) {
        direct_prepare_parameter_type(tp, parameter, preserve_outer_type);
        bool saved_preserve = tp->direct_scope_state
            ? tp->direct_scope_state->preserve_parameter_types : false;
        if (tp->direct_scope_state) {
            tp->direct_scope_state->preserve_parameter_types = true;
        }
        direct_bind_pattern(tp, parameter, JS_VAR_VAR, NULL);
        if (tp->direct_scope_state) {
            tp->direct_scope_state->preserve_parameter_types = saved_preserve;
        }
        direct_record_parameter_entries(tp, parameter);
    }
    for (JsAstNode* parameter = (JsAstNode*)function->params; parameter;
            parameter = parameter->next) {
        // Defaults are evaluated in the parameter environment, before the
        // function body is entered.
        direct_walk_pattern_defaults(tp, parameter);
        direct_clear_class_self_parameter(parameter, class_self_name);
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
        bool saved_preserve = tp->direct_scope_state
            ? tp->direct_scope_state->preserve_parameter_types : false;
        if (declarator->id && declarator->id->node_type !=
                JS_AST_NODE_IDENTIFIER && tp->direct_scope_state) {
            // Destructuring declarations are built through the reference
            // expression path, so their binding leaves retain any outer type
            // already observed while the pattern was constructed.
            tp->direct_scope_state->preserve_parameter_types = true;
            direct_prepare_parameter_type(tp, declarator->id, true);
        }
        direct_bind_pattern(tp, declarator->id,
            (JsVarKind)declaration->kind, owner);
        if (tp->direct_scope_state) {
            tp->direct_scope_state->preserve_parameter_types = saved_preserve;
        }
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
    bool saved_suppress = tp->direct_scope_state
        ? tp->direct_scope_state->suppress_pattern_keys : false;
    if (tp->direct_scope_state) tp->direct_scope_state->suppress_pattern_keys =
        true;
    direct_walk_node(tp, loop->init);
    if (tp->direct_scope_state) tp->direct_scope_state->suppress_pattern_keys =
        saved_suppress;
    direct_walk_node(tp, loop->test);
    direct_walk_node(tp, loop->update);
    direct_walk_node(tp, loop->body);
    js_scope_pop(tp);
}

static void direct_mark_pattern_for_in(JsAstNode* pattern) {
    if (!pattern) return;
    switch (pattern->node_type) {
    case JS_AST_NODE_IDENTIFIER:
        if (((JsIdentifierNode*)pattern)->entry) {
            ((JsIdentifierNode*)pattern)->entry->is_for_in_head = true;
        }
        break;
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        direct_mark_pattern_for_in(((JsAssignmentPatternNode*)pattern)->left);
        break;
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        direct_mark_pattern_for_in(((JsSpreadElementNode*)pattern)->argument);
        break;
    case JS_AST_NODE_ARRAY_PATTERN:
        for (JsAstNode* item = ((JsArrayPatternNode*)pattern)->elements;
                item; item = item->next) direct_mark_pattern_for_in(item);
        break;
    case JS_AST_NODE_OBJECT_PATTERN:
        for (JsAstNode* item = ((JsObjectPatternNode*)pattern)->properties;
                item; item = item->next) {
            direct_mark_pattern_for_in(item->node_type == JS_AST_NODE_PROPERTY
                ? ((JsPropertyNode*)item)->value : item);
        }
        break;
    default:
        break;
    }
}

static void direct_walk_for_of(JsTranspiler* tp, JsForOfNode* loop) {
    JsScope* scope = js_scope_create(tp, JS_SCOPE_BLOCK, tp->current_scope);
    if (!scope) return;
    loop->vars = scope;
    js_scope_push(tp, scope);
    bool saved_suppress = tp->direct_scope_state
        ? tp->direct_scope_state->suppress_pattern_keys : false;
    if (tp->direct_scope_state) tp->direct_scope_state->suppress_pattern_keys =
        true;
    if (loop->left && loop->left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        // Build the loop declaration's destructuring pattern before publishing
        // its lexical names, matching the reference adapter's binding order.
        direct_walk_node(tp, loop->left);
    } else if (loop->declares_binding) {
        bool saved_preserve = tp->direct_scope_state
            ? tp->direct_scope_state->preserve_parameter_types : false;
        if (loop->left && loop->left->node_type != JS_AST_NODE_IDENTIFIER &&
                tp->direct_scope_state) {
            // A parser-reduced destructuring loop head follows the same
            // expression-then-bind order as a reference variable declaration.
            tp->direct_scope_state->preserve_parameter_types = true;
            direct_prepare_parameter_type(tp, loop->left, true);
        }
        direct_bind_pattern(tp, loop->left, (JsVarKind)loop->kind, NULL);
        if (tp->direct_scope_state) {
            tp->direct_scope_state->preserve_parameter_types = saved_preserve;
        }
        // Loop-head defaults are evaluated in the loop environment; rebind
        // their references after the head names have been installed so they
        // do not retain parser-time scope entries.
        direct_walk_pattern_defaults(tp, loop->left);
        if (loop->node_type == JS_AST_NODE_FOR_IN_STATEMENT) {
            direct_mark_pattern_for_in(loop->left);
        }
        if (loop->left && loop->left->node_type == JS_AST_NODE_IDENTIFIER) {
            // A var loop redeclaration reuses the existing function binding;
            // preserve that binding's established type on the normalized head.
            JsIdentifierNode* id = (JsIdentifierNode*)loop->left;
            NameEntry* entry = id->entry;
            if (entry && entry->node && entry->node != (AstNode*)id &&
                    entry->node->type) id->type = entry->node->type;
        }
    } else {
        direct_walk_node(tp, loop->left);
    }
    if (tp->direct_scope_state) tp->direct_scope_state->suppress_pattern_keys =
        saved_suppress;
    bool saved_allow = tp->direct_scope_state
        ? tp->direct_scope_state->allow_for_head_keys : false;
    if (tp->direct_scope_state) tp->direct_scope_state->allow_for_head_keys =
        true;
    direct_walk_node(tp, loop->right);
    if (tp->direct_scope_state) tp->direct_scope_state->allow_for_head_keys =
        saved_allow;
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
    bool saved_preserve = tp->direct_scope_state
        ? tp->direct_scope_state->preserve_parameter_types : false;
    if (tp->direct_scope_state) {
        tp->direct_scope_state->preserve_parameter_types = true;
    }
    direct_prepare_parameter_type(tp, handler->param, true);
    direct_bind_pattern(tp, handler->param, JS_VAR_LET, NULL);
    if (tp->direct_scope_state) {
        tp->direct_scope_state->preserve_parameter_types = saved_preserve;
    }
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
        if (self) self->is_mutable = false;
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
            // Reference construction resolves a key only when its binding
            // was predeclared before the object was built; later scope repair
            // must preserve that construction-order fact.
            NameEntry* entry = js_scope_lookup(tp, key->name);
            bool visible = entry && direct_reference_key_is_visible(tp, entry,
                property->key->source_span, true);
            if (!visible && entry && entry->node &&
                    entry->node->node_type == JS_AST_NODE_IDENTIFIER &&
                    tp->direct_scope_state &&
                    tp->direct_scope_state->allow_for_head_keys) {
                visible = true;
            }
            if (visible) {
                direct_set_identifier(tp, key, entry);
            } else {
                key->entry = NULL;
                key->type = &TYPE_ANY;
            }
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
            // Class method labels follow the reference builder's expression
            // path, so a colliding outer binding remains attached to the key.
            JsIdentifierNode* key = (JsIdentifierNode*)method->key;
            direct_set_identifier(tp, key, js_scope_lookup(tp, key->name));
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
            // The reference builder resolves property identifiers through the
            // ordinary expression path, so a colliding lexical name remains
            // part of the indexed fact even though it is not a runtime read.
            JsIdentifierNode* property = (JsIdentifierNode*)member->property;
            NameEntry* entry = js_scope_lookup(tp, property->name);
            direct_set_identifier(tp, property, entry);
        }
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
    case JS_AST_NODE_ARRAY_PATTERN:
    case JS_AST_NODE_OBJECT_PATTERN:
        direct_walk_assignment_pattern(tp, node);
        break;
    case JS_AST_NODE_PARAMETER:
        direct_bind_pattern(tp, node, JS_VAR_VAR, NULL);
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
    case JS_AST_NODE_IMPORT_DECLARATION:
    case JS_AST_NODE_EXPORT_SPECIFIER:
    case JS_AST_NODE_IMPORT_SPECIFIER:
        break;
    case JS_AST_NODE_EXPORT_DECLARATION: {
        JsExportNode* export_node = (JsExportNode*)node;
        direct_walk_node(tp, export_node->declaration);
        break;
    }
    case TS_AST_NODE_PARAMETER: {
        TsParameterNode* parameter = (TsParameterNode*)node;
        direct_bind_pattern(tp, parameter->pattern, JS_VAR_VAR, NULL);
        direct_walk_node(tp, parameter->default_value);
        break;
    }
    case TS_AST_NODE_ENUM_DECLARATION:
        for (int i = 0; i < ((TsEnumDeclarationNode*)node)->member_count; i++) {
            direct_walk_node(tp, ((TsEnumDeclarationNode*)node)->members[i]);
        }
        break;
    case TS_AST_NODE_ENUM_MEMBER:
        direct_walk_node(tp, ((TsEnumMemberNode*)node)->initializer);
        break;
    case TS_AST_NODE_NAMESPACE_DECLARATION: {
        TsNamespaceDeclarationNode* ns = (TsNamespaceDeclarationNode*)node;
        for (int i = 0; i < ns->body_count; i++) direct_walk_node(tp, ns->body[i]);
        break;
    }
    case TS_AST_NODE_DECORATOR:
        direct_walk_node(tp, ((TsDecoratorNode*)node)->expression);
        break;
    case TS_AST_NODE_AS_EXPRESSION:
    case TS_AST_NODE_SATISFIES_EXPRESSION:
    case TS_AST_NODE_TYPE_ASSERTION: {
        TsTypeExprNode* expression = (TsTypeExprNode*)node;
        direct_walk_node(tp, expression->inner);
        break;
    }
    case TS_AST_NODE_NON_NULL_EXPRESSION:
        direct_walk_node(tp, ((TsNonNullNode*)node)->inner);
        break;
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
    JsDirectScopeState state = {arraylist_new(0), arraylist_new(0), false,
        false, false};
    tp->direct_scope_state = &state;
    direct_collect_predeclared_node(tp, ast);
    if (state.reference_predeclared) arraylist_sort(
        state.reference_predeclared, direct_compare_predeclared_facts);
    direct_walk_node(tp, ast);
    tp->current_scope = global;
    tp->direct_scope_state = NULL;
    if (state.reference_predeclared) arraylist_free(state.reference_predeclared);
    if (state.parameter_entries) arraylist_free(state.parameter_entries);
    return true;
}
