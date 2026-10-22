// js_ast_children.cpp — one description of JavaScript-only AST child edges.
//
// Core-shaped JavaScript nodes use the shared AST contract in ast-core.cpp.
// This table therefore covers only extension layouts; a walker keeps only the
// cases it finds interesting and delegates the rest to js_ast_visit_children.
//
// Ordering is part of the contract: children are visited in source order, and
// a walker that deliberately skips a child (e.g. not descending into a nested
// function) must say so as an explicit case rather than relying on the table.

#include "js_ast.hpp"
#include "../runtime/ast-core.hpp"
#include "../ts/ts_ast.hpp"
#include <string.h>

namespace {

enum ChildKind : uint8_t {
    CHILD_NODE = 0,  // a single child pointer
    CHILD_LIST = 1,  // head of a ->next-linked sibling list
};

// AST node structs use inheritance, so offsetof is not available on them; a
// captureless accessor lambda gives the same table with no layout assumption.
typedef JsAstNode** (*ChildAccessor)(JsAstNode*);

struct ChildSlot {
    ChildAccessor get;
    uint8_t kind;
};

struct ChildRow {
    JsAstNodeType type;
    uint8_t count;
    ChildSlot slots[4];
};

#define N(Type, field) \
    { [](JsAstNode* n) -> JsAstNode** { return (JsAstNode**)&((Type*)n)->field; }, CHILD_NODE }
#define L(Type, field) \
    { [](JsAstNode* n) -> JsAstNode** { return (JsAstNode**)&((Type*)n)->field; }, CHILD_LIST }

// Rows are looked up linearly; the table is small and the walkers that use it
// are compile-time passes, not a hot runtime path.
const ChildRow kChildRows[] = {
    { JS_AST_NODE_STATIC_BLOCK,          1, { N(JsStaticBlockNode, body) } },
    { JS_AST_NODE_LABELED_STATEMENT,     1, { N(JsLabeledStatementNode, body) } },
    { JS_AST_NODE_WITH_STATEMENT,        2, { N(JsWithStatementNode, object),
                                              N(JsWithStatementNode, body) } },
    { JS_AST_NODE_TEMPLATE_LITERAL,      2, { L(JsTemplateLiteralNode, quasis),
                                              L(JsTemplateLiteralNode, expressions) } },
    { JS_AST_NODE_TAGGED_TEMPLATE,       2, { N(JsTaggedTemplateNode, tag),
                                              N(JsTaggedTemplateNode, quasi) } },
    { (JsAstNodeType)TS_AST_NODE_TYPE_FACT, 0, {} },
    { (JsAstNodeType)TS_AST_NODE_PARAMETER, 2, { N(TsParameterNode, pattern),
                                               N(TsParameterNode, default_value) } },
};

#undef N
#undef L

const ChildRow* row_for(JsAstNodeType type) {
    for (size_t i = 0; i < sizeof(kChildRows) / sizeof(kChildRows[0]); i++) {
        if (kChildRows[i].type == type) return &kChildRows[i];
    }
    return NULL;
}

JsAstNode* child_at(JsAstNode* node, const ChildSlot& slot) {
    JsAstNode** location = slot.get(node);
    return location ? *location : NULL;
}

typedef bool (*ChildAction)(JsAstNode* child, void* ctx);

struct CoreVisitContext {
    JsAstChildVisit visit;
    JsAstChildPredicate predicate;
    void* ctx;
    bool found;
};

static void visit_core_child(AstNode* child, AstNode* parent, void* opaque) {
    CoreVisitContext* context = (CoreVisitContext*)opaque;
    if (!context || !child || (parent && child == parent->next)) return;
    for (AstNode* item = child; item; item = item->next) {
        JsAstNode* js_item = (JsAstNode*)item;
        if (context->predicate) {
            if (context->predicate(js_item, context->ctx)) context->found = true;
        } else if (context->visit) {
            context->visit(js_item, context->ctx);
        }
    }
}

static bool walk_child_row(JsAstNode* node, const ChildRow* row,
        ChildAction action, void* ctx) {
    if (node->node_type == AST_NODE_LOOP &&
            ((AstLoopControlNode*)node)->form == LOOP_FORM_DO_WHILE) {
        JsAstNode* body = child_at(node, row->slots[3]);
        JsAstNode* test = child_at(node, row->slots[1]);
        return (body && action(body, ctx)) || (test && action(test, ctx));
    }
    for (uint8_t i = 0; i < row->count; i++) {
        JsAstNode* child = child_at(node, row->slots[i]);
        if (row->slots[i].kind == CHILD_LIST) {
            for (JsAstNode* item = child; item; item = item->next) {
                if (action(item, ctx)) return true;
            }
        } else if (child && action(child, ctx)) {
            return true;
        }
    }
    return false;
}

struct VisitContext {
    JsAstChildVisit visit;
    void* ctx;
};

static void visit_binding_child(AstNode* child, AstNode*, void* opaque) {
    VisitContext* context = (VisitContext*)opaque;
    context->visit((JsAstNode*)child, context->ctx);
}

static bool predicate_binding_child(AstNode* child, void* opaque) {
    CoreVisitContext* context = (CoreVisitContext*)opaque;
    return context->predicate((JsAstNode*)child, context->ctx);
}

static bool visit_child(JsAstNode* child, void* opaque) {
    VisitContext* context = (VisitContext*)opaque;
    context->visit(child, context->ctx);
    return false;
}

}  // namespace

static bool js_ast_function_boundary(JsAstNode* node, JsAstNode* root) {
    return node != root && (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR || node->node_type == AST_NODE_ARROW_FUNC || node->node_type == AST_NODE_METHOD || node->node_type == AST_NODE_CLASS || node->node_type == AST_NODE_CLASS_EXPR);
}

static bool js_ast_lexical_function_boundary(JsAstNode* node, JsAstNode* root) {
    return node != root && (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR || node->node_type == AST_NODE_METHOD || node->node_type == AST_NODE_CLASS || node->node_type == AST_NODE_CLASS_EXPR);
}

static bool js_ast_call_is_direct_eval(JsAstNode* node) {
    if (!node || node->node_type != AST_NODE_CALL_EXPR) return false;
    JsCallNode* call = (JsCallNode*)node;
    if (call->optional || !call->callee ||
            call->callee->node_type != AST_NODE_IDENT) return false;
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    // `eval?.()` is always indirect eval; only the bare call is direct.
    return id->name && id->name->len == 4 &&
        strncmp(id->name->chars, "eval", 4) == 0;
}

struct JsAstFunctionFactWalk {
    JsAstFunctionFacts* facts;
    bool direct_eval_active;
    bool observations_active;
    bool with_active;
    bool direct_body_active;
    JsAstNode* parent;
};

JsAstNode* js_ast_class_element_key(JsAstNode* element, bool* computed, bool* statics) {
    *computed = false;
    if (statics) *statics = false;
    if (!element) return NULL;
    if (element->node_type == AST_NODE_METHOD) {
        JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)element;
        *computed = method->computed;
        if (statics) *statics = method->static_method;
        return method->key;
    }
    if (element->node_type == AST_NODE_FIELD) {
        JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)element;
        *computed = field->computed;
        if (statics) *statics = field->is_static;
        return field->key;
    }
    return NULL;
}

// T10-0/D-D: a key position is a name, not a reference. `node.arguments`,
// `{arguments: v}`, `class { arguments() {} }` and `obj.eval` must not be read
// as observations of `arguments`/`this`/`new.target` or as an `eval` mention;
// treating them as such materialized an arguments object on every call of the
// enclosing function (51 us/call on prettier_ast, which never uses `arguments`).
bool js_ast_fact_walk_skips_child(JsAstNode* parent, JsAstNode* child) {
    if (!parent || !child) return false;
    switch (parent->node_type) {
    case AST_NODE_MEMBER_EXPR: {
        JsMemberNode* member = (JsMemberNode*)parent;
        return !member->computed && (JsAstNode*)member->property == child;
    }
    case AST_NODE_PROPERTY: {
        JsPropertyNode* prop = (JsPropertyNode*)parent;
        // A shorthand `{arguments}` key doubles as the value reference.
        return !prop->computed && !prop->shorthand &&
            (JsAstNode*)prop->key == child;
    }
    case AST_NODE_METHOD: case AST_NODE_FIELD: {
        bool computed;
        JsAstNode* key = js_ast_class_element_key(parent, &computed);
        return !computed && key == child;
    }
    default:
        return false;
    }
}

static void js_ast_collect_function_facts_node(JsAstNode* node,
        JsAstFunctionFactWalk walk);

static void js_ast_collect_function_facts_child(JsAstNode* child, void* opaque) {
    JsAstFunctionFactWalk* walk = (JsAstFunctionFactWalk*)opaque;
    if (js_ast_fact_walk_skips_child(walk->parent, child)) return;
    js_ast_collect_function_facts_node(child, *walk);
}

static void js_ast_collect_function_facts_node(JsAstNode* node,
        JsAstFunctionFactWalk walk) {
    if (!node || !walk.facts) return;
    if (node->node_type == AST_NODE_CLASS || node->node_type == AST_NODE_CLASS_EXPR) {
        // heritage and computed keys execute in the enclosing function; initializers and methods do not.
        JsClassNode* cls = (JsClassNode*)node;
        js_ast_collect_function_facts_node(cls->superclass, walk);
        JsAstNode* members = cls->body;
        if (members && members->node_type == AST_NODE_BLOCK) members = ((JsBlockNode*)members)->statements;
        for (JsAstNode* member = members; member; member = member->next) {
            bool computed;
            JsAstNode* key = js_ast_class_element_key(member, &computed);
            if (computed) js_ast_collect_function_facts_node(key, walk);
        }
        return;
    }
    if (js_ast_function_boundary(node, NULL)) {
        walk.direct_eval_active = false;
        walk.with_active = false;
        if (js_ast_lexical_function_boundary(node, NULL)) {
            walk.observations_active = false;
        }
    }
    if (!walk.direct_eval_active && !walk.observations_active &&
            !walk.with_active) return;
    if (walk.direct_eval_active && js_ast_call_is_direct_eval(node)) {
        walk.facts->has_direct_eval = true;
    }
    if (walk.observations_active && js_ast_call_is_direct_eval(node)) {
        walk.facts->observes_direct_eval = true;
    }
    if (walk.direct_body_active && walk.direct_eval_active &&
            node->node_type == AST_NODE_CALL_EXPR) {
        JsCallNode* call = (JsCallNode*)node;
        if (js_ast_identifier_named(call->callee, "super", 5) &&
                (!walk.facts->has_direct_super_call ||
                 node->source_span.start_byte < walk.facts->first_direct_super_call_start)) {
            walk.facts->has_direct_super_call = true;
            walk.facts->first_direct_super_call_start = node->source_span.start_byte;
        }
    }
    if (walk.observations_active) {
        if (js_ast_identifier_named(node, "arguments", 9)) {
            walk.facts->observations |= JS_AST_OBSERVES_ARGUMENTS;
        } else if (js_ast_identifier_named(node, "this", 4)) {
            walk.facts->observations |= JS_AST_OBSERVES_THIS;
        } else if (js_ast_identifier_named(node, "new.target", 10)) {
            walk.facts->observations |= JS_AST_OBSERVES_NEW_TARGET;
        }
        if (node->node_type == AST_NODE_CALL_EXPR ||
                node->node_type == AST_NODE_NEW_EXPR) {
            JsCallNode* call = (JsCallNode*)node;
            if (js_ast_identifier_named(call->callee, "super", 5)) {
                walk.facts->observations |= JS_AST_OBSERVES_THIS;
                // direct_eval_active is cleared at every function boundary, so
                // its absence here means this super() sits inside a nested
                // arrow that still shares the constructor's `this`.
                if (!walk.direct_eval_active) {
                    walk.facts->has_lexical_super_call = true;
                }
            }
        } else if (node->node_type == AST_NODE_MEMBER_EXPR) {
            JsMemberNode* member = (JsMemberNode*)node;
            if (js_ast_identifier_named(member->object, "super", 5)) {
                walk.facts->observations |= JS_AST_OBSERVES_THIS | JS_AST_OBSERVES_SUPER_PROPERTY;
            }
        }
    }
    if (walk.with_active && node->node_type == JS_AST_NODE_WITH_STATEMENT) {
        walk.facts->has_with = true;
    }
    walk.parent = node;
    js_ast_visit_children(node, js_ast_collect_function_facts_child, &walk);
}

JsAstFunctionFacts js_ast_collect_function_facts(JsAstNode* params,
        JsAstNode* body) {
    JsAstFunctionFacts facts = {};
    for (JsAstNode* param = params; param;
            param = (JsAstNode*)param->next) {
        js_ast_collect_function_facts_node(param, {&facts, true, true, false, false, NULL});
    }
    js_ast_collect_function_facts_node(body,
        {&facts, true, true, true, true, NULL});
    return facts;
}

bool js_ast_publish_extension_facts(AstNode* node, struct AstIndex* index) {
    if (!node || !index) return true;
    switch (node->node_type) {
    case AST_NODE_IF_EXPR: {
        JsIfNode* n = (JsIfNode*)node;
        return ast_index_publish_scope(index, n->consequent_vars) &&
            ast_index_publish_scope(index, n->alternate_vars);
    }
    case AST_NODE_CLASS_EXPR: return ast_index_publish_scope(index, ((JsClassNode*)node)->expression_scope);
    case AST_NODE_CATCH_CLAUSE: return ast_index_publish_scope(index, ((JsCatchNode*)node)->vars);
    case AST_NODE_MATCH_EXPR: return ast_index_publish_scope(index, ((JsSwitchNode*)node)->vars);
    case AST_NODE_FOR_OF_STAM: case AST_NODE_FOR_IN_STAM:
        return ast_index_publish_scope(index, ((JsForOfNode*)node)->vars);
    default:
        return true;
    }
}

void js_ast_visit_children(JsAstNode* node, JsAstChildVisit visit, void* ctx) {
    if (!node || !visit) return;
    if (node->node_type < JS_AST_NODE_TEMPLATE_LITERAL) {
        CoreVisitContext context = {visit, NULL, ctx, false};
        ast_visit_core_children((AstNode*)node, visit_core_child, &context);
        return;
    }
    const ChildRow* row = row_for(node->node_type);
    if (!row) return;
    VisitContext context = {visit, ctx};
    walk_child_row(node, row, visit_child, &context);
}

bool js_ast_any_child(JsAstNode* node, JsAstChildPredicate predicate, void* ctx) {
    if (!node || !predicate) return false;
    if (node->node_type < JS_AST_NODE_TEMPLATE_LITERAL) {
        CoreVisitContext context = {NULL, predicate, ctx, false};
        ast_visit_core_children((AstNode*)node, visit_core_child, &context);
        return context.found;
    }
    const ChildRow* row = row_for(node->node_type);
    if (!row) return false;
    return walk_child_row(node, row, predicate, ctx);
}

void js_ast_visit_binding_pattern_children(JsAstNode* node,
        JsAstChildVisit visit, void* ctx) {
    if (!visit) return;
    JsAstNode* pattern = js_ast_parameter_pattern(node);
    if (pattern != node) {
        visit(pattern, ctx);
        return;
    }
    VisitContext context = {visit, ctx};
    ast_visit_binding_pattern_children((AstNode*)node, visit_binding_child,
        &context);
}

bool js_ast_any_binding_pattern_child(JsAstNode* node,
        JsAstChildPredicate predicate, void* ctx) {
    if (!predicate) return false;
    JsAstNode* pattern = js_ast_parameter_pattern(node);
    if (pattern != node) return predicate(pattern, ctx);
    CoreVisitContext context = {NULL, predicate, ctx, false};
    return ast_any_binding_pattern_child((AstNode*)node,
        predicate_binding_child, &context);
}

JsAstNode* js_ast_pattern_reference_target(JsAstNode* pattern) {
    if (pattern && pattern->node_type == AST_NODE_ASSIGN_PATTERN)
        pattern = ((JsAssignmentPatternNode*)pattern)->left;
    return pattern && (pattern->node_type == AST_NODE_IDENT ||
        pattern->node_type == AST_NODE_MEMBER_EXPR ||
        pattern->node_type == AST_NODE_INDEX_EXPR) ? pattern : NULL;
}

JsAstNode* js_ast_parameter_pattern(JsAstNode* parameter) {
    return parameter && parameter->node_type == (int)TS_AST_NODE_PARAMETER
        ? ((TsParameterNode*)parameter)->pattern : parameter;
}

JsAstNode* js_ast_parameter_default_value(JsAstNode* parameter) {
    if (!parameter) return NULL;
    if (parameter->node_type == (int)TS_AST_NODE_PARAMETER)
        return ((TsParameterNode*)parameter)->default_value;
    return parameter->node_type == AST_NODE_ASSIGN_PATTERN
        ? ((JsAssignmentPatternNode*)parameter)->right : NULL;
}

JsIdentifierNode* js_ast_parameter_binding_identifier(JsAstNode* parameter) {
    parameter = js_ast_parameter_pattern(parameter);
    if (!parameter) return NULL;
    if (parameter->node_type == AST_NODE_IDENT) {
        return (JsIdentifierNode*)parameter;
    }
    if (parameter->node_type == AST_NODE_ASSIGN_PATTERN) {
        return js_ast_parameter_binding_identifier(
            ((JsAssignmentPatternNode*)parameter)->left);
    }
    if (parameter->node_type == AST_NODE_REST_ELEMENT ||
            parameter->node_type == AST_NODE_SPREAD) {
        return js_ast_parameter_binding_identifier(
            ((JsSpreadElementNode*)parameter)->argument);
    }
    return NULL;
}

static bool js_ast_parameter_has_expression(JsAstNode* parameter, bool computed_keys);

static bool js_ast_parameter_expression_child(JsAstNode* child, void* opaque) {
    return js_ast_parameter_has_expression(child, *(bool*)opaque);
}

static bool js_ast_parameter_has_expression(JsAstNode* parameter, bool computed_keys) {
    if (!parameter) return false;
    if (js_ast_parameter_default_value(parameter)) return true;
    parameter = js_ast_parameter_pattern(parameter);
    // computed binding keys execute in the parameter environment even without defaults.
    if (computed_keys && parameter->node_type == AST_NODE_MAP_PATTERN) {
        for (JsAstNode* property = ((JsObjectPatternNode*)parameter)->properties;
                property; property = property->next) {
            if (property->node_type == AST_NODE_PROPERTY &&
                    ((JsPropertyNode*)property)->computed) return true;
        }
    }
    return js_ast_any_binding_pattern_child(parameter,
        js_ast_parameter_expression_child, &computed_keys);
}

static bool js_ast_parameter_names_equal(JsAstNode* left, JsAstNode* right) {
    JsIdentifierNode* left_id = js_ast_parameter_binding_identifier(left);
    JsIdentifierNode* right_id = js_ast_parameter_binding_identifier(right);
    return left_id && right_id && left_id->name && right_id->name &&
        left_id->name->len == right_id->name->len &&
        memcmp(left_id->name->chars, right_id->name->chars,
            left_id->name->len) == 0;
}

JsAstParameterFacts js_ast_collect_parameter_facts(JsAstNode* parameters) {
    JsAstParameterFacts facts = {};
    int formal_count = 0;
    bool formal_ended = false;
    JsAstNode* last_parameter = NULL;
    for (JsAstNode* parameter = parameters; parameter;
            parameter = parameter->next) {
        JsAstNode* pattern = js_ast_parameter_pattern(parameter);
        JsAstNode* default_value = js_ast_parameter_default_value(parameter);
        facts.parameter_count++;
        if (!formal_ended) {
            bool ends_formal_length =
                pattern->node_type == AST_NODE_REST_ELEMENT ||
                pattern->node_type == AST_NODE_SPREAD || default_value;
            if (ends_formal_length) {
                facts.formal_length = formal_count;
                formal_ended = true;
            } else {
                formal_count++;
            }
        }
        facts.has_default_params = facts.has_default_params ||
            js_ast_parameter_has_expression(parameter, false);
        facts.has_parameter_expressions = facts.has_parameter_expressions ||
            js_ast_parameter_has_expression(parameter, true);
        if (default_value || pattern->node_type != AST_NODE_IDENT) {
            facts.has_non_simple_params = true;
        }
        for (JsAstNode* later = parameter->next; later;
                later = later->next) {
            if (js_ast_parameter_names_equal(parameter, later)) {
                facts.has_duplicate_param_names = true;
                facts.first_duplicate_param =
                    js_ast_parameter_binding_identifier(later);
                break;
            }
        }
        last_parameter = pattern;
    }
    if (last_parameter &&
            (last_parameter->node_type == AST_NODE_REST_ELEMENT ||
             last_parameter->node_type == AST_NODE_SPREAD)) {
        facts.has_rest_param = true;
        facts.has_non_simple_params = true;
    }
    return facts;
}

bool js_ast_child_catalog_complete(void) {
    static const JsAstNodeType extension_types[] = {
        JS_AST_NODE_STATIC_BLOCK, JS_AST_NODE_LABELED_STATEMENT,
        JS_AST_NODE_WITH_STATEMENT, JS_AST_NODE_TEMPLATE_LITERAL,
        JS_AST_NODE_TAGGED_TEMPLATE,
        (JsAstNodeType)TS_AST_NODE_TYPE_FACT, (JsAstNodeType)TS_AST_NODE_PARAMETER,
    };
    const size_t row_count = sizeof(kChildRows) / sizeof(kChildRows[0]);
    if (row_count != sizeof(extension_types) / sizeof(extension_types[0])) return false;
    for (size_t i = 0; i < row_count; i++) {
        if (!row_for(extension_types[i])) return false;
    }
    return true;
}

void js_ast_visit_extension_children(AstNode* node, AstChildVisitor visitor,
                                     void* ctx) {
    if (!node || !visitor) return;

    if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        return;
    }
    if (node->node_type == AST_NODE_FUNC ||
            node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_ARROW_FUNC ||
            node->node_type == AST_NODE_METHOD) {
        return;
    }

    if (node->node_type < JS_AST_NODE_TEMPLATE_LITERAL) return;
    const ChildRow* row = row_for(node->node_type);
    if (!row) return;
    for (uint8_t i = 0; i < row->count; i++) {
        JsAstNode* child = child_at((JsAstNode*)node, row->slots[i]);
        // AstIndex's visitor descends through each child's `next` link. Pass
        // only the list head here so the common walker stays the sole sibling
        // traversal authority.
        if (child) visitor((AstNode*)child, node, ctx);
    }
}

// A suspension is lowered into this body when it descends from it with no
// function boundary in between. The indexed owner is the usual answer, but its
// span-based recovery pulls a computed class-member key into the *method's*
// function even though the key is a child of the member and is evaluated in the
// enclosing scope. Trusting structure as well charges that key's `await` to the
// function that actually evaluates it.
static bool js_ast_suspension_is_lowered_here(const AstIndex* index,
        AstNodeId node_id, AstNodeId root_id) {
    AstNodeId id = node_id;
    AstNode* from = NULL;
    while (id != AST_NODE_ID_INVALID && id != root_id) {
        AstNode* node = index->nodes[id];
        if (from && ast_index_node_is_function(node)) {
            // A computed method key is an AST child of the method node, yet it
            // is evaluated — and lowered — in the enclosing scope. The walk
            // passes through that one edge instead of stopping at it; every
            // other function edge ends the search.
            if (node->node_type != AST_NODE_METHOD ||
                    ((AstMethodNode*)node)->key != from) {
                return false;
            }
        }
        from = node;
        id = ast_index_parent_id(index, id);
    }
    return id == root_id;
}

bool js_ast_index_can_suspend(const AstIndex* index, AstNodeId root_id,
        AstNodeId begin_id, AstNodeId end_id, JsSuspensionKind kind) {
    if (!index || root_id == AST_NODE_ID_INVALID || begin_id == AST_NODE_ID_INVALID ||
            end_id == AST_NODE_ID_INVALID) {
        return false;
    }
    AstFunctionId owner = index->owner_functions[root_id];
    for (uint32_t i = begin_id; i < end_id; i++) {
        AstNode* node = index->nodes[i];
        if (!node) continue;
        bool suspends = kind == JS_SUSPENSION_YIELD
            ? node->node_type == AST_NODE_YIELD
            : node->node_type == AST_NODE_AWAIT ||
              (node->node_type == AST_NODE_FOR_OF_STAM &&
               ((JsForOfNode*)node)->is_await);
        if (!suspends) continue;
        if (index->owner_functions[i] == owner ||
                js_ast_suspension_is_lowered_here(index, i, root_id)) return true;
    }
    return false;
}

// Check if an expression subtree contains an optional chain (?.),
// meaning the result may be undefined due to short-circuiting.
bool js_ast_has_optional_chain(JsAstNode* node) {
    if (!node) return false;
    if (node->node_type == AST_NODE_MEMBER_EXPR || node->node_type == AST_NODE_INDEX_EXPR) {
        JsMemberNode* m = (JsMemberNode*)node;
        if (m->optional) return true;
        return js_ast_chain_continues(node, m->object) && js_ast_has_optional_chain(m->object);
    }
    if (node->node_type == AST_NODE_CALL_EXPR) {
        JsCallNode* c = (JsCallNode*)node;
        if (c->optional) return true;
        return js_ast_chain_continues(node, c->callee) && js_ast_has_optional_chain(c->callee);
    }
    return false;
}

bool js_ast_is_proto_literal_key(JsAstNode* key) {
    if (!key) return false;
    if (key->node_type == AST_NODE_IDENT) {
        JsIdentifierNode* id = (JsIdentifierNode*)key;
        return id->name && id->name->len == 9 &&
            memcmp(id->name->chars, "__proto__", 9) == 0;
    }
    if (key->node_type == AST_NODE_LITERAL) {
        JsLiteralNode* literal = (JsLiteralNode*)key;
        String* value = literal->literal_type == AST_LITERAL_STRING ?
            literal->value.string_value : NULL;
        return value && value->len == 9 &&
            memcmp(value->chars, "__proto__", 9) == 0;
    }
    return false;
}

bool js_ast_property_is_proto_setter(const JsPropertyNode* property) {
    return property && !property->computed && !property->method &&
        !property->is_getter && !property->is_setter && !property->shorthand &&
        property->key && property->value && property->key != property->value &&
        js_ast_is_proto_literal_key(property->key);
}
