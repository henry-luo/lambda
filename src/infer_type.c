#include "transpiler.h"

LambdaTypeId infer_primary_expr(Transpiler* tp, TSNode pri_node) {
    printf("infer primary expr\n");
    TSNode child = ts_node_named_child(pri_node, 0);
    if (ts_node_is_null(child)) { return LMD_TYPE_NULL; }
    TSSymbol symbol = ts_node_symbol(child);
    if (symbol == tp->SYM_NULL) {
        return LMD_TYPE_NULL;
    }
    else if (symbol == tp->SYM_TRUE || symbol == tp->SYM_FALSE) {
        return LMD_TYPE_BOOL;
    }
    else if (symbol == tp->SYM_NUMBER) {
        return LMD_TYPE_INT;
    }
    else if (symbol == tp->SYM_STRING) {
        return LMD_TYPE_STRING;
    }
    else if (symbol == tp->SYM_NULL) {
        return LMD_TYPE_NULL;
    }
}

LambdaTypeId infer_binary_expr(Transpiler* tp, TSNode bi_node) {
    printf("infer binary expr\n");
    TSNode left_node = ts_node_child_by_field_id(bi_node, tp->ID_LEFT);
    LambdaTypeId left = infer_expr(tp, left_node);

    TSNode op_node = ts_node_child_by_field_name(bi_node, "operator", 8);

    TSNode right_node = ts_node_child_by_field_id(bi_node, tp->ID_RIGHT);
    LambdaTypeId right = infer_expr(tp, right_node);

    if (left == right) {
        return left;
    }
    else {
        printf("type mismatch: %d vs %d\n", left, right);
        return LMD_TYPE_NULL;
    }
}

LambdaTypeId infer_if_expr(Transpiler* tp, TSNode if_node) {
    TSNode then_node = ts_node_child_by_field_id(if_node, tp->ID_THEN);
    LambdaTypeId then_type = infer_expr(tp, then_node);
    TSNode else_node = ts_node_child_by_field_id(if_node, tp->ID_ELSE);
    LambdaTypeId else_type = infer_expr(tp, else_node);
    if (then_type == else_type) {
        return then_type;
    }
    else {
        printf("type mismatch: %d vs %d\n", then_type, else_type);
        return LMD_TYPE_NULL;
    }
}

LambdaTypeId infer_assignment_expr(Transpiler* tp, TSNode asn_node) {
    return LMD_TYPE_NULL;
}

LambdaTypeId infer_let_expr(Transpiler* tp, TSNode let_node) {
    printf("infer let expr\n");
    TSNode then_node = ts_node_child_by_field_id(let_node, tp->ID_THEN);
    return infer_expr(tp, then_node);
}

LambdaTypeId infer_expr(Transpiler* tp, TSNode expr_node) {
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
        return LMD_TYPE_NULL;
    }
}