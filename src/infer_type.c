#include "transpiler.h"
#include "../lib/hashmap.h"

// create a type cache that maps node id to the inferred type using lib/hashmap
typedef struct NodeTypeEntry {
    void* node_id;
    LambdaType type;
} NodeTypeEntry;

int node_type_entry_compare(const void *a, const void *b, void *udata) {
    const NodeTypeEntry *na = a;
    const NodeTypeEntry *nb = b;
    printf("before hash compare: %p, %p\n", na->node_id, nb->node_id);
    return na->node_id == nb->node_id;
}

uint64_t node_type_entry_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const NodeTypeEntry *node_entry = item;
    printf("before hasing node id: %p\n", node_entry->node_id);
    return hashmap_xxhash3(node_entry->node_id, sizeof(void*), seed0, seed1);
}

LambdaType* get_node_type(Transpiler* tp, TSNode node) {
    printf("get node type: %p\n", node.id);
    // check the hashmap first
    if (tp->node_type_map == NULL) {
        tp->node_type_map = hashmap_new(sizeof(NodeTypeEntry), 10, 0, 0, 
        node_type_entry_hash, node_type_entry_compare, NULL, NULL);
    }

    printf("before hashmap_get\n");
    NodeTypeEntry* entry = (NodeTypeEntry*) hashmap_get(tp->node_type_map, 
        &(NodeTypeEntry){.node_id = node.id});
    if (entry) {
        printf("Node type loaded from cache: %p\n", entry->node_id);
        return &entry->type;
    }
    else {
        printf("Node not found in cache: %p\n", node.id);
        return NULL;
    }
}

void set_node_type(Transpiler* tp, TSNode node, LambdaType type) {
    printf("set node type: %p\n", node.id);
    if (tp->node_type_map == NULL) {
        tp->node_type_map = hashmap_new(sizeof(NodeTypeEntry), 10, 0, 0, 
        node_type_entry_hash, node_type_entry_compare, NULL, NULL);
    }
    hashmap_set(tp->node_type_map, &(NodeTypeEntry){.node_id = node.id, .type = type});
}

LambdaType NULL_TYPE = {.type = LMD_TYPE_NULL, .nested = NULL, .length = 0};

LambdaType infer_primary_expr(Transpiler* tp, TSNode pri_node) {
    LambdaType* rtype = get_node_type(tp, pri_node);
    if (rtype) { return *rtype; }

    printf("infer primary expr\n");
    TSNode child = ts_node_named_child(pri_node, 0);
    if (ts_node_is_null(child)) { return NULL_TYPE; }
    TSSymbol symbol = ts_node_symbol(child);
    LambdaType type;
    if (symbol == tp->SYM_NULL) {
        type = NULL_TYPE;
    }
    else if (symbol == tp->SYM_TRUE || symbol == tp->SYM_FALSE) {
        type = (LambdaType){.type = LMD_TYPE_BOOL, .nested = NULL, .length = 0};
    }
    else if (symbol == tp->SYM_NUMBER) {
        type = (LambdaType){.type = LMD_TYPE_INT, .nested = NULL, .length = 0};
    }
    else if (symbol == tp->SYM_STRING) {
        type = (LambdaType){.type = LMD_TYPE_STRING, .nested = NULL, .length = 0};
    }
    else if (symbol == tp->SYM_NULL) {
        type = NULL_TYPE;
    }
    else {
        type = NULL_TYPE;
    }
    set_node_type(tp, pri_node, type);
    return type;
}

LambdaType infer_binary_expr(Transpiler* tp, TSNode bi_node) {
    printf("infer binary expr\n");
    TSNode left_node = ts_node_child_by_field_id(bi_node, tp->ID_LEFT);
    LambdaType left = infer_expr(tp, left_node);

    TSNode op_node = ts_node_child_by_field_name(bi_node, "operator", 8);

    TSNode right_node = ts_node_child_by_field_id(bi_node, tp->ID_RIGHT);
    LambdaType right = infer_expr(tp, right_node);

    if (left.type == right.type) {
        return left;
    }
    else {
        printf("type mismatch: %d vs %d\n", left, right);
        return NULL_TYPE;
    }
}

LambdaType infer_if_expr(Transpiler* tp, TSNode if_node) {
    TSNode then_node = ts_node_child_by_field_id(if_node, tp->ID_THEN);
    LambdaType then_type = infer_expr(tp, then_node);
    TSNode else_node = ts_node_child_by_field_id(if_node, tp->ID_ELSE);
    LambdaType else_type = infer_expr(tp, else_node);
    if (then_type.type == else_type.type) {
        return then_type;
    }
    else {
        printf("type mismatch: %d vs %d\n", then_type.type, else_type.type);
        return NULL_TYPE;
    }
}

LambdaType infer_assignment_expr(Transpiler* tp, TSNode asn_node) {
    return NULL_TYPE;
}

LambdaType infer_let_expr(Transpiler* tp, TSNode let_node) {
    printf("infer let expr\n");
    TSNode then_node = ts_node_child_by_field_id(let_node, tp->ID_THEN);
    return infer_expr(tp, then_node);
}

LambdaType infer_expr(Transpiler* tp, TSNode expr_node) {
    // get the function name
    TSSymbol symbol = ts_node_symbol(expr_node);
    if (symbol == tp->SYM_IF_EXPR) {
        return infer_if_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_BINARY_EXPR) {
        return infer_binary_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_PRIMARY_EXPR) {
        return infer_primary_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_LET_EXPR) {
        return infer_let_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_ASSIGNMENT_EXPR) {
        return infer_assignment_expr(tp, expr_node);
    }
    else {
        printf("unknown expr %s\n", ts_node_type(expr_node));
        return NULL_TYPE;
    }
}