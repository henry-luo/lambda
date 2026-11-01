#pragma once

#include "ast.hpp"

// shared literal query; no source value or loop fact is inferred from a machine register.
static inline bool mir_decimal_int_literal(const char* source, AstNode* node, int64_t* out) {
    node = ast_unwrap_primary_to_leaf(node);
    if (!source || !node || !node->type || node->type->type_id != LMD_TYPE_INT) return false;
    uint32_t start = node->source_span.start_byte, end = node->source_span.end_byte;
    if (end <= start || end - start > 18) return false;
    const char* text = source + start;
    uint32_t i = text[0] == '-' ? 1 : 0;
    if (i >= end - start) return false;
    int64_t value = 0;
    for (; i < end - start; i++) {
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10 + (text[i] - '0');
    }
    *out = text[0] == '-' ? -value : value;
    return true;
}

enum { MIR_FINITE_DESCENDING_SUM_MAX = 100000000 };
static constexpr int64_t MIR_FINITE_DESCENDING_SUM_TOTAL_MAX =
    (int64_t)MIR_FINITE_DESCENDING_SUM_MAX * (MIR_FINITE_DESCENDING_SUM_MAX + 1) / 2;
static_assert(MIR_FINITE_DESCENDING_SUM_TOTAL_MAX <= INT53_MAX, "countdown sum must remain finite");

struct MirFiniteDescendingSumPlan {
    NameEntry* counter;
    NameEntry* accumulator;
};

// Recognize only `while (n >= 0) { s = s + n; n = n - 1 }` in that order.
// Callers prove native local storage and a zero-origin accumulator separately.
// No intervening expression, alias, call, transfer or second write is admitted.
static inline bool mir_descending_sum_pattern(const char* source,
        AstLoopControlNode* loop, MirFiniteDescendingSumPlan* plan) {
    if (!loop || !plan || !loop->body || loop->body->node_type != AST_NODE_CONTENT) return false;
    AstNode* condition = ast_unwrap_primary(loop->cond);
    if (!condition || condition->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* compare = (AstBinaryNode*)condition;
    AstNode* counter_node = ast_unwrap_primary(compare->left);
    int64_t literal = 0;
    if (compare->op != OPERATOR_GE || !counter_node || counter_node->node_type != AST_NODE_IDENT ||
            !mir_decimal_int_literal(source, compare->right, &literal) || literal != 0) return false;
    NameEntry* counter = ((AstIdentNode*)counter_node)->entry;
    AstNode* first = ((AstListNode*)loop->body)->item;
    AstNode* last = first ? first->next : NULL;
    if (!counter || !first || !last || last->next || first->node_type != AST_NODE_ASSIGN_STAM ||
            last->node_type != AST_NODE_ASSIGN_STAM) return false;
    AstAssignStamNode* sum = (AstAssignStamNode*)first;
    AstAssignStamNode* step = (AstAssignStamNode*)last;
    if (!sum->target_entry || sum->target_entry == counter || step->target_entry != counter ||
            sum->object || sum->key || step->object || step->key) return false;
    AstNode* add_node = ast_unwrap_primary(sum->value);
    AstNode* sub_node = ast_unwrap_primary(step->value);
    if (!add_node || !sub_node || add_node->node_type != AST_NODE_BINARY ||
            sub_node->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* add = (AstBinaryNode*)add_node;
    AstBinaryNode* sub = (AstBinaryNode*)sub_node;
    AstNode* reads[] = {ast_unwrap_primary(add->left), ast_unwrap_primary(add->right),
        ast_unwrap_primary(sub->left)};
    NameEntry* expected[] = {sum->target_entry, counter, counter};
    for (int i = 0; i < 3; i++)
        if (!reads[i] || reads[i]->node_type != AST_NODE_IDENT ||
                ((AstIdentNode*)reads[i])->entry != expected[i]) return false;
    if (add->op != OPERATOR_ADD || sub->op != OPERATOR_SUB ||
            !mir_decimal_int_literal(source, sub->right, &literal) || literal != 1) return false;
    *plan = {counter, sum->target_entry};
    return true;
}
