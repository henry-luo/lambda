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
    return node != root && (node->node_type == JS_AST_NODE_FUNCTION_DECLARATION || node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION || node->node_type == JS_AST_NODE_ARROW_FUNCTION || node->node_type == JS_AST_NODE_METHOD_DEFINITION || node->node_type == JS_AST_NODE_CLASS_DECLARATION || node->node_type == JS_AST_NODE_CLASS_EXPRESSION);
}

static bool js_ast_lexical_function_boundary(JsAstNode* node, JsAstNode* root) {
    return node != root && (node->node_type == JS_AST_NODE_FUNCTION_DECLARATION || node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION || node->node_type == JS_AST_NODE_METHOD_DEFINITION || node->node_type == JS_AST_NODE_CLASS_DECLARATION || node->node_type == JS_AST_NODE_CLASS_EXPRESSION);
}

static bool js_ast_call_is_direct_eval(JsAstNode* node) {
    if (!node || node->node_type != JS_AST_NODE_CALL_EXPRESSION) return false;
    JsCallNode* call = (JsCallNode*)node;
    if (call->optional || !call->callee ||
            call->callee->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    // `eval?.()` is always indirect eval; only the bare call is direct.
    return id->name && id->name->len == 4 &&
        strncmp(id->name->chars, "eval", 4) == 0;
}

static bool js_ast_identifier_named(JsAstNode* node, const char* name,
        size_t length) {
    if (!node || node->node_type != JS_AST_NODE_IDENTIFIER) return false;
    String* value = ((JsIdentifierNode*)node)->name;
    return value && value->len == length && strncmp(value->chars, name, length) == 0;
}

struct JsAstFunctionFactWalk {
    JsAstFunctionFacts* facts;
    bool direct_eval_active;
    bool observations_active;
    bool with_active;
    bool tail_active;
    bool direct_body_active;
};

static void js_ast_collect_function_facts_node(JsAstNode* node,
        JsAstFunctionFactWalk walk);

static void js_ast_collect_function_facts_child(JsAstNode* child, void* opaque) {
    js_ast_collect_function_facts_node(child,
        *(JsAstFunctionFactWalk*)opaque);
}

static void js_ast_collect_function_facts_node(JsAstNode* node,
        JsAstFunctionFactWalk walk) {
    if (!node || !walk.facts) return;
    if (js_ast_function_boundary(node, NULL)) {
        if (walk.tail_active) walk.facts->tail_reuse_safe = false;
        walk.direct_eval_active = false;
        walk.with_active = false;
        if (js_ast_lexical_function_boundary(node, NULL)) {
            walk.observations_active = false;
        }
    }
    if (!walk.direct_eval_active && !walk.observations_active &&
            !walk.with_active && !walk.tail_active) return;
    if (walk.direct_eval_active && js_ast_call_is_direct_eval(node)) {
        walk.facts->has_direct_eval = true;
    }
    if (walk.direct_body_active && walk.direct_eval_active &&
            node->node_type == JS_AST_NODE_CALL_EXPRESSION) {
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
        if (node->node_type == JS_AST_NODE_CALL_EXPRESSION ||
                node->node_type == JS_AST_NODE_NEW_EXPRESSION) {
            JsCallNode* call = (JsCallNode*)node;
            if (js_ast_identifier_named(call->callee, "super", 5)) {
                walk.facts->observations |= JS_AST_OBSERVES_THIS;
            }
        } else if (node->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
            JsMemberNode* member = (JsMemberNode*)node;
            if (js_ast_identifier_named(member->object, "super", 5)) {
                walk.facts->observations |= JS_AST_OBSERVES_THIS;
            }
        }
    }
    if (walk.with_active && node->node_type == JS_AST_NODE_WITH_STATEMENT) {
        walk.facts->has_with = true;
    }
    if (walk.tail_active && (node->node_type == JS_AST_NODE_WITH_STATEMENT ||
            node->node_type == AST_NODE_TRY_STAM ||
            js_ast_identifier_named(node, "eval", 4))) {
        walk.facts->tail_reuse_safe = false;
    }
    js_ast_visit_children(node, js_ast_collect_function_facts_child, &walk);
}

JsAstFunctionFacts js_ast_collect_function_facts(JsAstNode* params,
        JsAstNode* body) {
    JsAstFunctionFacts facts = {};
    facts.tail_reuse_safe = true;
    for (JsAstNode* param = params; param;
            param = (JsAstNode*)param->next) {
        js_ast_collect_function_facts_node(param, {&facts, true, true, false, true, false});
    }
    js_ast_collect_function_facts_node(body,
        {&facts, true, true, true, true, true});
    facts.tail_reuse_safe = facts.tail_reuse_safe &&
        !(facts.observations & JS_AST_OBSERVES_ARGUMENTS);
    return facts;
}

bool js_ast_publish_extension_facts(AstNode* node, struct AstIndex* index) {
    if (!node || !index) return true;
    switch (node->node_type) {
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        return ast_index_publish_scope(index, n->consequent_vars) &&
            ast_index_publish_scope(index, n->alternate_vars);
    }
    case JS_AST_NODE_CLASS_EXPRESSION: return ast_index_publish_scope(index, ((JsClassNode*)node)->expression_scope);
    case JS_AST_NODE_CATCH_CLAUSE: return ast_index_publish_scope(index, ((JsCatchNode*)node)->vars);
    case JS_AST_NODE_SWITCH_STATEMENT: return ast_index_publish_scope(index, ((JsSwitchNode*)node)->vars);
    case JS_AST_NODE_FOR_OF_STATEMENT: case JS_AST_NODE_FOR_IN_STATEMENT:
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
    VisitContext context = {visit, ctx};
    ast_visit_binding_pattern_children((AstNode*)node, visit_binding_child,
        &context);
}

bool js_ast_any_binding_pattern_child(JsAstNode* node,
        JsAstChildPredicate predicate, void* ctx) {
    if (!predicate) return false;
    CoreVisitContext context = {NULL, predicate, ctx, false};
    return ast_any_binding_pattern_child((AstNode*)node,
        predicate_binding_child, &context);
}

JsIdentifierNode* js_ast_parameter_binding_identifier(JsAstNode* parameter) {
    if (!parameter) return NULL;
    if (parameter->node_type == JS_AST_NODE_IDENTIFIER) {
        return (JsIdentifierNode*)parameter;
    }
    if (parameter->node_type == (int)TS_AST_NODE_PARAMETER) {
        return js_ast_parameter_binding_identifier(
            ((TsParameterNode*)parameter)->pattern);
    }
    if (parameter->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
        return js_ast_parameter_binding_identifier(
            ((JsAssignmentPatternNode*)parameter)->left);
    }
    if (parameter->node_type == JS_AST_NODE_REST_ELEMENT ||
            parameter->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
        return js_ast_parameter_binding_identifier(
            ((JsSpreadElementNode*)parameter)->argument);
    }
    return NULL;
}

static bool js_ast_parameter_has_default_value(JsAstNode* parameter);

static bool js_ast_parameter_default_child(JsAstNode* child, void*) {
    return js_ast_parameter_has_default_value(child);
}

static bool js_ast_parameter_has_default_value(JsAstNode* parameter) {
    return parameter &&
        (parameter->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN ||
         js_ast_any_binding_pattern_child(parameter,
             js_ast_parameter_default_child, NULL));
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
        if (!formal_ended) {
            bool ends_formal_length =
                parameter->node_type == JS_AST_NODE_REST_ELEMENT ||
                parameter->node_type == JS_AST_NODE_SPREAD_ELEMENT ||
                parameter->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN;
            if (parameter->node_type == (int)TS_AST_NODE_PARAMETER) {
                ends_formal_length = ends_formal_length ||
                    ((TsParameterNode*)parameter)->default_value;
            }
            if (ends_formal_length) {
                facts.formal_length = formal_count;
                formal_ended = true;
            } else {
                formal_count++;
            }
        }
        facts.has_default_params = facts.has_default_params ||
            js_ast_parameter_has_default_value(parameter);
        if (parameter->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN ||
            parameter->node_type == JS_AST_NODE_ARRAY_PATTERN ||
            parameter->node_type == JS_AST_NODE_OBJECT_PATTERN ||
            parameter->node_type == JS_AST_NODE_REST_ELEMENT ||
            parameter->node_type == JS_AST_NODE_SPREAD_ELEMENT ||
            (parameter->node_type != JS_AST_NODE_IDENTIFIER &&
             parameter->node_type != (int)TS_AST_NODE_PARAMETER)) {
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
        last_parameter = parameter;
    }
    if (last_parameter &&
            (last_parameter->node_type == JS_AST_NODE_REST_ELEMENT ||
             last_parameter->node_type == JS_AST_NODE_SPREAD_ELEMENT)) {
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

    if (node->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
        return;
    }
    if (node->node_type == JS_AST_NODE_FUNCTION_DECLARATION ||
            node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
            node->node_type == JS_AST_NODE_ARROW_FUNCTION ||
            node->node_type == JS_AST_NODE_METHOD_DEFINITION) {
        return;
    }

    // AstNodeType owns the storage field while TypeScript reserves values in
    // the same numeric domain. Compare through the shared scalar domain so
    // the TS extension range does not mix unrelated enum types.
    int node_type = node->node_type;
    if (node_type >= TS_AST_NODE_TYPE_FACT &&
            node_type < TS_AST_NODE__MAX) {
        switch (node_type) {
        case TS_AST_NODE_PARAMETER: {
            TsParameterNode* parameter = (TsParameterNode*)node;
            if (parameter->pattern) visitor((AstNode*)parameter->pattern, node,
                ctx);
            if (parameter->default_value) visitor(
                (AstNode*)parameter->default_value, node, ctx);
            break;
        }
        default:
            // The remaining TS leaves use scalar metadata or a reference name.
            break;
        }
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
