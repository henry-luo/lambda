#include "transpiler.h"
#include "../lib/hashmap.h"

LambdaType NULL_TYPE = {.type = LMD_TYPE_NULL, .nested = NULL, .length = 0};
LambdaType BOOL_TYPE = {.type = LMD_TYPE_BOOL, .nested = NULL, .length = 0};
LambdaType INT_TYPE = {.type = LMD_TYPE_INT, .nested = NULL, .length = 0};
LambdaType FLOAT_TYPE = {.type = LMD_TYPE_FLOAT, .nested = NULL, .length = 0};
LambdaType STRING_TYPE = {.type = LMD_TYPE_STRING, .nested = NULL, .length = 0};

AstNode* alloc_ast_node(Transpiler* tp, AstNodeType node_type, TSNode node, size_t size) {
    AstNode* ast_node;
    pool_variable_alloc(tp->ast_node_pool, size, &ast_node);
    if (!ast_node) { return NULL; }
    memset(ast_node, 0, size);
    ast_node->node_type = node_type;  ast_node->node = node;  
    // ast_node->type = NULL_TYPE;
    return ast_node;
}

AstNode* build_primary_expr(Transpiler* tp, TSNode pri_node) {
    printf("build primary expr\n");
    AstNode* ast_node = alloc_ast_node(tp, AST_NODE_PRIMARY, pri_node, sizeof(AstNode));
    TSNode child = ts_node_named_child(pri_node, 0);
    if (ts_node_is_null(child)) { return ast_node; }

    // infer data type
    TSSymbol symbol = ts_node_symbol(child);
    if (symbol == tp->SYM_NULL) {
        ast_node->type = NULL_TYPE;
    }
    else if (symbol == tp->SYM_TRUE || symbol == tp->SYM_FALSE) {
        ast_node->type = BOOL_TYPE;
    }
    else if (symbol == tp->SYM_NUMBER) {
        ast_node->type = INT_TYPE;
    }
    else if (symbol == tp->SYM_STRING) {
        ast_node->type = STRING_TYPE;
    }
    else if (symbol == tp->SYM_NULL) {
        ast_node->type = NULL_TYPE;
    }
    else {
        ast_node->type = NULL_TYPE;
    }
    return ast_node;
}

AstNode* build_binary_expr(Transpiler* tp, TSNode bi_node) {
    printf("build binary expr\n");
    AstBinaryNode* ast_node = (AstBinaryNode*)alloc_ast_node(tp, AST_NODE_BINARY, bi_node, sizeof(AstBinaryNode));
    TSNode left_node = ts_node_child_by_field_id(bi_node, tp->ID_LEFT);
    ast_node->left = build_expr(tp, left_node);

    // TSNode op_node = ts_node_child_by_field_name(bi_node, "operator", 8);

    TSNode right_node = ts_node_child_by_field_id(bi_node, tp->ID_RIGHT);
    ast_node->right = build_expr(tp, right_node);

    ast_node->type = ast_node->left->type;
    return ast_node;
}

AstNode* build_if_expr(Transpiler* tp, TSNode if_node) {
    printf("build if expr\n");
    AstIfExprNode* ast_node = (AstIfExprNode*)alloc_ast_node(tp, AST_NODE_IF_EXPR, if_node, sizeof(AstIfExprNode));
    TSNode cond_node = ts_node_child_by_field_id(if_node, tp->ID_COND);
    ast_node->cond = build_expr(tp, cond_node);
    TSNode then_node = ts_node_child_by_field_id(if_node, tp->ID_THEN);
    ast_node->then = build_expr(tp, then_node);
    TSNode else_node = ts_node_child_by_field_id(if_node, tp->ID_ELSE);
    ast_node->otherwise = build_expr(tp, else_node);
    return ast_node;
}

AstNode* build_assign_expr(Transpiler* tp, TSNode asn_node) {
    printf("build assign expr\n");
    AstAssignNode* ast_node = alloc_ast_node(tp, AST_NODE_ASSIGN, asn_node, sizeof(AstAssignNode));

    ast_node->name = ts_node_child_by_field_id(asn_node, tp->ID_NAME);
    if (ts_node_is_null(ast_node->name)) { printf("no identifier found\n"); return NULL; }

    TSNode val_node = ts_node_child_by_field_id(asn_node, tp->ID_BODY);
    if (ts_node_is_null(val_node)) { printf("no value found\n"); return NULL; }
    ast_node->expr = build_expr(tp, val_node);
    return ast_node;
}

AstNode* build_let_expr(Transpiler* tp, TSNode let_node) {
    printf("build let expr\n");
    AstLetExprNode* ast_node = (AstLetExprNode*)alloc_ast_node(tp, AST_NODE_LET_EXPR, let_node, sizeof(AstLetExprNode));

    // let can have multiple cond declarations
    TSTreeCursor cursor = ts_tree_cursor_new(let_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode *prev_declare = NULL;
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == tp->ID_DECLARE) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode *declare = build_expr(tp, child);
            printf("got declare type %d\n", declare->node_type);
            if (prev_declare == NULL) {
                ast_node->declare = declare;
            } else {
                prev_declare->next = declare;
            }
            prev_declare = declare;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    if (!ast_node->declare) { printf("missing let declare\n"); }

    TSNode then_node = ts_node_child_by_field_id(let_node, tp->ID_THEN);
    ast_node->then = build_expr(tp, then_node);
    if (!ast_node->then) { printf("missing let then\n"); }
    else { printf("got let then type %d\n", ast_node->then->node_type); }
    return ast_node;
}

AstNode* build_array_expr(Transpiler* tp, TSNode array_node) {
    printf("build array expr\n");
    AstArrayNode* ast_node = (AstArrayNode*)alloc_ast_node(tp, AST_NODE_ARRAY, array_node, sizeof(AstArrayNode));
    ast_node->type.type = LMD_TYPE_ARRAY;
    TSTreeCursor cursor = ts_tree_cursor_new(array_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        TSNode child = ts_tree_cursor_current_node(&cursor);
        AstNode* item = build_expr(tp, child);
        if (!ast_node->item) ast_node->item = item;
        else {
            ast_node->item->next = item;
        }
        ast_node->type.length++;
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    return ast_node;
}

AstNode* build_func(Transpiler* tp, TSNode func_node) {
    printf("infer function\n");
    AstFuncNode* ast_node = (AstFuncNode*)alloc_ast_node(tp, AST_NODE_FUNC, func_node, sizeof(AstFuncNode));
    // get the function name
    TSNode fn_name_node = ts_node_child_by_field_id(func_node, tp->ID_NAME);
    ast_node->name = fn_name_node;
    // get the function body
    TSNode fn_body_node = ts_node_child_by_field_id(func_node, tp->ID_BODY);
    ast_node->body = build_expr(tp, fn_body_node);
    return ast_node;
}

AstNode* build_expr(Transpiler* tp, TSNode expr_node) {
    // get the function name
    TSSymbol symbol = ts_node_symbol(expr_node);
    if (symbol == tp->SYM_IF_EXPR) {
        return build_if_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_BINARY_EXPR) {
        return build_binary_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_PRIMARY_EXPR) {
        return build_primary_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_LET_EXPR) {
        return build_let_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_ASSIGNMENT_EXPR) {
        return build_assign_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_ARRAY) {
        return build_array_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_FUNC) {
        return build_func(tp, expr_node);
    }
    else {
        printf("unknown expr %s\n", ts_node_type(expr_node));
        return NULL;
    }
}

AstNode* print_ast_node(AstNode *node, int indent) {
    for (int i = 0; i < indent; i++) { printf("  "); }
    // get the function name
    switch(node->node_type) {
    case AST_NODE_IF_EXPR:
        printf("[if expr]\n");
        print_ast_node(((AstIfExprNode*)node)->cond, indent + 1);
        print_ast_node(((AstIfExprNode*)node)->then, indent + 1);
        print_ast_node(((AstIfExprNode*)node)->otherwise, indent + 1);
        break;
    case AST_NODE_BINARY:
        printf("[binary expr]\n");
        print_ast_node(((AstBinaryNode*)node)->left, indent + 1);
        print_ast_node(((AstBinaryNode*)node)->right, indent + 1);
        break;
    case AST_NODE_LET_EXPR:
        printf("[let expr]\n");
        AstNode *declare = ((AstLetExprNode*)node)->declare;
        while (declare) {
            for (int i = 0; i < indent+1; i++) { printf("  "); }
            printf("declare:\n");
            print_ast_node(declare, indent + 1);
            declare = declare->next;
        }
        for (int i = 0; i < indent+1; i++) { printf("  "); }
        printf("then:\n");
        print_ast_node(((AstLetExprNode*)node)->then, indent + 1);
        break;
    case AST_NODE_ASSIGN:
        printf("[assign expr]\n");
        print_ast_node(((AstAssignNode*)node)->expr, indent + 1);
        break;
    case AST_NODE_ARRAY:
        printf("[array expr]\n");
        print_ast_node(((AstArrayNode*)node)->item, indent + 1);
        break;
    case AST_NODE_FUNC:
        printf("[function expr]\n");
        print_ast_node(((AstFuncNode*)node)->body, indent + 1);
        break;
    case AST_NODE_PRIMARY:
        printf("[primary expr]\n");
        break;
    default:
        printf("unknown expression type\n");
        break;
    }
}