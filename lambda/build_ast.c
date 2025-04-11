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

void* alloc_ast_bytes(Transpiler* tp, size_t size) {
    void* bytes;
    pool_variable_alloc(tp->ast_node_pool, size, &bytes);
    if (!bytes) { return NULL; }
    memset(bytes, 0, size);
    return bytes;
}

AstNode* build_array_expr(Transpiler* tp, TSNode array_node) {
    printf("build array expr\n");
    AstArrayNode* ast_node = (AstArrayNode*)alloc_ast_node(tp, AST_NODE_ARRAY, array_node, sizeof(AstArrayNode));
    ast_node->type.type = LMD_TYPE_ARRAY;
    TSNode child = ts_node_named_child(array_node, 0);
    AstNode* prev_item = NULL;  LambdaType *nested_type = NULL;
    while (!ts_node_is_null(child)) {
        AstNode* item = build_expr(tp, child);
        if (!prev_item) { 
            ast_node->item = item;  nested_type = &item->type;
        } else {  
            prev_item->next = item;
            if (nested_type && item->type.type != nested_type->type) {
                nested_type = NULL;  // type mismatch, reset the nested type to NULL
            }
        }
        prev_item = item;
        ast_node->type.length++;
        child = ts_node_next_named_sibling(child);
    }
    ast_node->type.nested = nested_type;
    return ast_node;
}

AstNode* build_field_expr(Transpiler* tp, TSNode array_node) {
    printf("build field expr\n");
    AstFieldNode* ast_node = (AstFieldNode*)alloc_ast_node(tp, AST_NODE_FIELD_EXPR, array_node, sizeof(AstFieldNode));
    TSNode object_node = ts_node_child_by_field_id(array_node, tp->ID_OBJECT);
    ast_node->object = build_expr(tp, object_node);
    TSNode field_node = ts_node_child_by_field_id(array_node, tp->ID_FIELD);
    ast_node->field = build_expr(tp, field_node);
    if (ast_node->object->type.type == LMD_TYPE_ARRAY) {
        ast_node->type = *ast_node->object->type.nested;
    }
    else if (ast_node->object->type.type == LMD_TYPE_MAP) {
        ast_node->type = *ast_node->object->type.nested;
    }
    else {
        ast_node->type = ast_node->object->type;
    }
    return ast_node;
}

LambdaType build_identifier(Transpiler* tp, TSNode id_node) {
    printf("building identifier\n");
    // get the identifier name
    // check if the name is in the name stack
    StrView var_name = {tp->source + ts_node_start_byte(id_node), 
        ts_node_end_byte(id_node) - ts_node_start_byte(id_node)};
    NameScope *scope = tp->current_scope;
    FIND_VAR_NAME:
    NameEntry *entry = scope->first;
    while (entry) {
        if (strview_eq(&entry->name, &var_name)) { break; }
        entry = entry->next;
    }
    if (!entry) {
        if (tp->current_scope->parent) {
            scope = tp->current_scope->parent;
            goto FIND_VAR_NAME;
        }
        printf("missing identifier %.*s\n", var_name.length, var_name.str);
        return NULL_TYPE;
    }
    else {
        printf("found identifier %.*s\n", entry->name.length, entry->name.str);
        return entry->node->type;
    }
}

AstNode* build_primary_expr(Transpiler* tp, TSNode pri_node) {
    printf("build primary expr\n");
    AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, pri_node, sizeof(AstPrimaryNode));
    TSNode child = ts_node_named_child(pri_node, 0);
    if (ts_node_is_null(child)) { return ast_node; }

    // infer data type
    TSSymbol symbol = ts_node_symbol(child);
    printf("primary expr symbol %d\n", symbol);
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
    else if (symbol == tp->SYM_IDENTIFIER) {
        ast_node->type = build_identifier(tp, child);
    }
    else if (symbol == tp->SYM_ARRAY) {
        ast_node->expr = build_array_expr(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == tp->SYM_MEMBER_EXPR) {
        ast_node->expr = build_field_expr(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == tp->SYM_SUBSCRIPT_EXPR) {
        ast_node->expr = build_field_expr(tp, child);
        ast_node->type = ast_node->expr->type;
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
    // determine the type of the if expression
    ast_node->type = ast_node->then->type;
    return ast_node;
}

AstNode* build_let_expr(Transpiler* tp, TSNode let_node) {
    printf("build let expr\n");
    AstLetNode* ast_node = (AstLetNode*)alloc_ast_node(tp, AST_NODE_LET_EXPR, let_node, sizeof(AstLetNode));

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

    // determine let node type
    ast_node->type = ast_node->then->type;
    return ast_node;
}

AstNode* build_assign_expr(Transpiler* tp, TSNode asn_node) {
    printf("build assign expr\n");
    AstAssignNode* ast_node = alloc_ast_node(tp, AST_NODE_ASSIGN, asn_node, sizeof(AstAssignNode));

    TSNode name = ts_node_child_by_field_id(asn_node, tp->ID_NAME);
    int start_byte = ts_node_start_byte(name);
    ast_node->name.str = tp->source + start_byte;
    ast_node->name.length = ts_node_end_byte(name) - start_byte;

    TSNode val_node = ts_node_child_by_field_id(asn_node, tp->ID_THEN);
    ast_node->then = build_expr(tp, val_node);

    // determine the type of the variable
    ast_node->type = ast_node->then->type;

    // push the name to the name stack
    printf("pushing name %.*s\n", ast_node->name.length, ast_node->name.str);
    NameEntry *entry = (NameEntry*)alloc_ast_bytes(tp, sizeof(NameEntry));
    entry->name = ast_node->name;  entry->node = ast_node;
    if (!tp->current_scope->first) { tp->current_scope->first = entry; }
    if (!tp->current_scope->last) { tp->current_scope->last = entry; }
    else { tp->current_scope->last->next = entry; }
    tp->current_scope->last = entry;
    return ast_node;
}

AstNode* build_let_stam(Transpiler* tp, TSNode let_node) {
    printf("build let expr\n");
    AstLetNode* ast_node = (AstLetNode*)alloc_ast_node(tp, AST_NODE_LET_STAM, let_node, sizeof(AstLetNode));

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

    // let statement does not have 'then' clause
    return ast_node;
}

AstNode* build_func(Transpiler* tp, TSNode func_node) {
    printf("infer function\n");
    AstFuncNode* ast_node = (AstFuncNode*)alloc_ast_node(tp, AST_NODE_FUNC, func_node, sizeof(AstFuncNode));
    ast_node->type.type = LMD_TYPE_FUNC;
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
    else if (symbol == tp->SYM_LET_STAM) {
        return build_let_stam(tp, expr_node);
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
    case AST_NODE_LET_EXPR:  case AST_NODE_LET_STAM:
        printf("[let %s]\n", node->node_type == AST_NODE_LET_EXPR ? "expr" : "stam");
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            for (int i = 0; i < indent+1; i++) { printf("  "); }
            printf("declare:\n");
            print_ast_node(declare, indent + 1);
            declare = declare->next;
        }
        if (node->node_type == AST_NODE_LET_EXPR) {
            for (int i = 0; i < indent+1; i++) { printf("  "); }
            printf("then:\n");
            print_ast_node(((AstLetNode*)node)->then, indent + 1);
        }
        break;
    case AST_NODE_ASSIGN:
        printf("[assign expr]\n");
        print_ast_node(((AstAssignNode*)node)->then, indent + 1);
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
        if (((AstPrimaryNode*)node)->expr) {
            print_ast_node(((AstPrimaryNode*)node)->expr, indent + 1);
        }
        break;
    case AST_SCRIPT:
        printf("[script]\n");
        AstScript* child = ((AstScript*)node)->child;
        while (child) {
            print_ast_node(child, indent + 1);
            child = child->next;
        }
        break;
    default:
        printf("unknown expression type\n");
        break;
    }
}

AstNode* build_script(Transpiler* tp, TSNode script_node) {
    printf("build script\n");
    AstScript* ast_node = (AstScript*)alloc_ast_node(tp, AST_SCRIPT, script_node, sizeof(AstScript));
    tp->current_scope = ast_node->global_vars = (NameScope*)alloc_ast_bytes(tp, sizeof(NameScope));

    // build the script body
    TSNode child = ts_node_named_child(script_node, 0);
    AstNode* prev = NULL;
    while (!ts_node_is_null(child)) {
        AstNode* ast = build_expr(tp, child);
        if (!prev) ast_node->child = ast;
        else { prev->next = ast; }
        prev = ast;
        child = ts_node_next_named_sibling(child);
    }
    return ast_node;
}