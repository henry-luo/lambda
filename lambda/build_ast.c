#include "transpiler.h"
#include "../lib/hashmap.h"

LambdaType TYPE_ANY = {.type_id = LMD_TYPE_ANY, .is_const = 0};
LambdaType TYPE_ERROR = {.type_id = LMD_TYPE_ERROR, .is_const = 0};
LambdaType TYPE_BOOL = {.type_id = LMD_TYPE_BOOL, .is_const = 0};
LambdaType TYPE_INT = {.type_id = LMD_TYPE_INT, .is_const = 0};
LambdaType TYPE_FLOAT = {.type_id = LMD_TYPE_FLOAT, .is_const = 0};
LambdaType TYPE_DOUBLE = {.type_id = LMD_TYPE_DOUBLE, .is_const = 0};
LambdaType TYPE_STRING = {.type_id = LMD_TYPE_STRING, .is_const = 0};
LambdaType TYPE_FUNC = {.type_id = LMD_TYPE_FUNC, .is_const = 0};

LambdaType CONST_BOOL = {.type_id = LMD_TYPE_BOOL, .is_const = 1};
LambdaType CONST_INT = {.type_id = LMD_TYPE_INT, .is_const = 1};
LambdaType CONST_FLOAT = {.type_id = LMD_TYPE_FLOAT, .is_const = 1};
LambdaType CONST_DOUBLE = {.type_id = LMD_TYPE_DOUBLE, .is_const = 1};
LambdaType CONST_STRING = {.type_id = LMD_TYPE_STRING, .is_const = 1};

LambdaType LIT_NULL = {.type_id = LMD_TYPE_NULL, .is_const = 1, .is_literal = 1};
LambdaType LIT_BOOL = {.type_id = LMD_TYPE_BOOL, .is_const = 1, .is_literal = 1};
LambdaType LIT_INT = {.type_id = LMD_TYPE_INT, .is_const = 1, .is_literal = 1};
LambdaType LIT_FLOAT = {.type_id = LMD_TYPE_FLOAT, .is_const = 1, .is_literal = 1};
LambdaType LIT_DOUBLE = {.type_id = LMD_TYPE_DOUBLE, .is_const = 1, .is_literal = 1};
LambdaType LIT_STRING = {.type_id = LMD_TYPE_STRING, .is_const = 1, .is_literal = 1};

int byte_size[] = {
    [LMD_RAW_POINTER] = sizeof(void*),
    [LMD_TYPE_NULL] = sizeof(bool),
    [LMD_TYPE_ANY] = sizeof(void*),
    [LMD_TYPE_ERROR] = sizeof(void*),
    [LMD_TYPE_BOOL] = sizeof(bool),
    [LMD_TYPE_INT] = sizeof(long),
    [LMD_TYPE_FLOAT] = sizeof(double),
    [LMD_TYPE_STRING] = sizeof(char*),
    [LMD_TYPE_ARRAY] = sizeof(void*),
    [LMD_TYPE_MAP] = sizeof(void*),
    [LMD_TYPE_ELEMENT] = sizeof(void*),
    [LMD_TYPE_FUNC] = sizeof(void*),
};

AstNode* alloc_ast_node(Transpiler* tp, AstNodeType node_type, TSNode node, size_t size) {
    AstNode* ast_node;
    pool_variable_alloc(tp->ast_pool, size, (void**)&ast_node);
    memset(ast_node, 0, size);
    ast_node->node_type = node_type;  ast_node->node = node;
    return ast_node;
}

void* alloc_ast_bytes(Transpiler* tp, size_t size) {
    void* bytes;
    pool_variable_alloc(tp->ast_pool, size, &bytes);
    memset(bytes, 0, size);
    return bytes;
}

void* alloc_const(Transpiler* tp, size_t size) {
    void* bytes;
    pool_variable_alloc(tp->ast_pool, size, &bytes);
    memset(bytes, 0, size);
    return bytes;
}

LambdaType* alloc_type(Transpiler* tp, TypeId type, size_t size) {
    LambdaType* t;
    pool_variable_alloc(tp->ast_pool, size, (void**)&t);
    memset(t, 0, size);
    t->type_id = type;  // t->is_const = 0;
    return t;
}

AstNode* build_array_expr(Transpiler* tp, TSNode array_node) {
    printf("build array expr\n");
    AstArrayNode* ast_node = (AstArrayNode*)alloc_ast_node(tp, AST_NODE_ARRAY, array_node, sizeof(AstArrayNode));
    ast_node->type = alloc_type(tp, LMD_TYPE_ARRAY, sizeof(LambdaTypeArray));
    LambdaTypeArray *type = (LambdaTypeArray*)ast_node->type;
    TSNode child = ts_node_named_child(array_node, 0);
    AstNode* prev_item = NULL;  LambdaType *nested_type = NULL;
    while (!ts_node_is_null(child)) {
        AstNode* item = build_expr(tp, child);
        if (!prev_item) { 
            ast_node->item = item;  nested_type = item->type;
        } else {  
            prev_item->next = item;
            if (nested_type && item->type->type_id != nested_type->type_id) {
                nested_type = NULL;  // type mismatch, reset the nested type to NULL
            }
        }
        prev_item = item;
        type->length++;
        child = ts_node_next_named_sibling(child);
    }
    type->nested = nested_type;
    return (AstNode*)ast_node;
}

AstNode* build_field_expr(Transpiler* tp, TSNode array_node) {
    printf("build field expr\n");
    AstFieldNode* ast_node = (AstFieldNode*)alloc_ast_node(tp, AST_NODE_FIELD_EXPR, array_node, sizeof(AstFieldNode));
    TSNode object_node = ts_node_child_by_field_id(array_node, FIELD_OBJECT);
    ast_node->object = build_expr(tp, object_node);

    TSNode field_node = ts_node_child_by_field_id(array_node, FIELD_FIELD);
    ast_node->field = build_expr(tp, field_node);

    if (ast_node->object->type->type_id == LMD_TYPE_ARRAY) {
        ast_node->type = ((LambdaTypeArray*)ast_node->object->type)->nested;
    }
    else if (ast_node->object->type->type_id == LMD_TYPE_MAP) {
        ast_node->type = &TYPE_ANY;
    }
    else {
        ast_node->type = &TYPE_ANY;
    }
    return (AstNode*)ast_node;
}

AstNode* build_call_expr(Transpiler* tp, TSNode call_node) {
    printf("build call expr\n");
    AstCallNode* ast_node = (AstCallNode*)alloc_ast_node(tp, AST_NODE_CALL_EXPR, call_node, sizeof(AstCallNode));
    TSNode function_node = ts_node_child_by_field_id(call_node, FIELD_FUNCTION);
    ast_node->function = build_expr(tp, function_node);

    TSTreeCursor cursor = ts_tree_cursor_new(call_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode *prev_argument = NULL;
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_ARGUMENT) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode *argument = build_expr(tp, child);
            printf("got argument type %d\n", argument->node_type);
            if (prev_argument == NULL) {
                ast_node->argument = argument;
            } else {
                prev_argument->next = argument;
            }
            prev_argument = argument;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    ast_node->type = &TYPE_ANY;
    return (AstNode*)ast_node;
}

AstNode* build_identifier(Transpiler* tp, TSNode id_node) {
    printf("building identifier\n");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_IDENT, id_node, sizeof(AstNamedNode));

    // get the identifier name
    StrView var_name = ts_node_source(tp, id_node);
    ast_node->name = var_name;

    // lookup the name
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
        printf("missing identifier %.*s\n", (int)var_name.length, var_name.str);
        ast_node->type = &TYPE_ERROR;
    }
    else {
        printf("found identifier %.*s\n", (int)entry->name.length, entry->name.str);
        ast_node->then = entry->node;
        ast_node->type = entry->node->type;
    }
    return (AstNode*)ast_node;
}

AstNode* build_primary_expr(Transpiler* tp, TSNode pri_node) {
    printf("build primary expr\n");
    AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, pri_node, sizeof(AstPrimaryNode));
    TSNode child = ts_node_named_child(pri_node, 0);
    if (ts_node_is_null(child)) { return (AstNode*)ast_node; }

    // infer data type
    TSSymbol symbol = ts_node_symbol(child);
    printf("primary expr symbol %d\n", symbol);
    if (symbol == SYM_NULL) {
        ast_node->type = &LIT_NULL;
    }
    else if (symbol == SYM_TRUE || symbol == SYM_FALSE) {
        ast_node->type = &LIT_BOOL;
    }
    else if (symbol == SYM_INT) {
        ast_node->type = &LIT_INT;
    }
    else if (symbol == SYM_FLOAT) {
        LambdaTypeItem *item_type = (LambdaTypeItem *)alloc_type(tp, LMD_TYPE_DOUBLE, sizeof(LambdaTypeItem));
        const char* num_str = tp->source + ts_node_start_byte(child);
        item_type->double_val = atof(num_str);
        arraylist_append(tp->const_list, &item_type->double_val);
        item_type->const_index = tp->const_list->length - 1;
        item_type->is_const = 1;  item_type->is_literal = 1;
        ast_node->type = (LambdaType *)item_type;
    }
    else if (symbol == SYM_STRING || symbol == SYM_SYMBOL || symbol == SYM_DATETIME) {
        // todo: exclude zero-length string
        int start = ts_node_start_byte(child), end = ts_node_end_byte(child);
        int len =  end - start - (symbol == SYM_DATETIME ? 3 : 2);  // -2 to exclude the quotes
        LambdaTypeString *str_type = (LambdaTypeString*)alloc_type(tp, 
            symbol == SYM_DATETIME ? LMD_TYPE_DTIME :
            symbol == SYM_STRING ? LMD_TYPE_STRING : LMD_TYPE_SYMBOL, sizeof(LambdaTypeString));
        str_type->is_const = 1;  str_type->is_literal = 1;
        ast_node->type = (LambdaType *)str_type;
        // copy the string, todo: handle escape sequence
        pool_variable_alloc(tp->ast_pool, sizeof(String) + len + 1, (void **)&str_type->string);
        const char* str_content = tp->source + start + (symbol == SYM_DATETIME ? 2:1);
        memcpy(str_type->string->str, str_content, len);  // memcpy is probably faster than strcpy
        str_type->string->str[len] = '\0';
        str_type->string->len = len;
        // add to const list
        arraylist_append(tp->const_list, str_type->string);
        str_type->const_index = tp->const_list->length - 1;
        printf("const string: %p, len %d, index %d\n", str_type->string, len, str_type->const_index);
    }
    else if (symbol == SYM_IDENT) {
        ast_node->expr = build_identifier(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_ARRAY) {
        ast_node->expr = build_array_expr(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_MAP) {
        ast_node->expr = build_map_expr(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_MEMBER_EXPR) {
        ast_node->expr = build_field_expr(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_SUBSCRIPT_EXPR) {
        ast_node->expr = build_field_expr(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_CALL_EXPR) {
        ast_node->expr = build_call_expr(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else { // from _parenthesized_expr
        ast_node->expr = build_expr(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    return (AstNode*)ast_node;
}

AstNode* build_binary_expr(Transpiler* tp, TSNode bi_node) {
    printf("build binary expr\n");
    AstBinaryNode* ast_node = (AstBinaryNode*)alloc_ast_node(tp, AST_NODE_BINARY, bi_node, sizeof(AstBinaryNode));
    TSNode left_node = ts_node_child_by_field_id(bi_node, FIELD_LEFT);
    ast_node->left = build_expr(tp, left_node);

    TSNode op_node = ts_node_child_by_field_id(bi_node, FIELD_OPERATOR);
    StrView op = ts_node_source(tp, op_node);  
    ast_node->operator = op;
    if (strview_equal(&op, "and")) { ast_node->op = OPERATOR_AND; }
    else if (strview_equal(&op, "or")) { ast_node->op = OPERATOR_OR; }
    else if (strview_equal(&op, "+")) { ast_node->op = OPERATOR_ADD; }
    else if (strview_equal(&op, "-")) { ast_node->op = OPERATOR_SUB; }
    else if (strview_equal(&op, "*")) { ast_node->op = OPERATOR_MUL; }
    else if (strview_equal(&op, "**")) { ast_node->op = OPERATOR_POW; }
    else if (strview_equal(&op, "/")) { ast_node->op = OPERATOR_DIV; }
    else if (strview_equal(&op, "_/")) { ast_node->op = OPERATOR_IDIV; }
    else if (strview_equal(&op, "%")) { ast_node->op = OPERATOR_MOD; }
    else if (strview_equal(&op, "==")) { ast_node->op = OPERATOR_EQ; }
    else if (strview_equal(&op, "!=")) { ast_node->op = OPERATOR_NE; }
    else if (strview_equal(&op, "<")) { ast_node->op = OPERATOR_LT; }
    else if (strview_equal(&op, "<=")) { ast_node->op = OPERATOR_LE; }
    else if (strview_equal(&op, ">")) { ast_node->op = OPERATOR_GT; }
    else if (strview_equal(&op, ">=")) { ast_node->op = OPERATOR_GE; }
    else { printf("unknown operator: %.*s\n", (int)op.length, op.str); }

    TSNode right_node = ts_node_child_by_field_id(bi_node, FIELD_RIGHT);
    ast_node->right = build_expr(tp, right_node);

    TypeId type_id;
    if (ast_node->op == OPERATOR_MUL || ast_node->op == OPERATOR_DIV || ast_node->op == OPERATOR_POW) {
        type_id = LMD_TYPE_DOUBLE;
    } else if (ast_node->op == OPERATOR_ADD || ast_node->op == OPERATOR_SUB || ast_node->op == OPERATOR_MOD) {
        type_id = max(ast_node->left->type->type_id, ast_node->right->type->type_id);
    } else if (ast_node->op == OPERATOR_AND || ast_node->op == OPERATOR_OR || 
        ast_node->op == OPERATOR_EQ || ast_node->op == OPERATOR_NE || 
        ast_node->op == OPERATOR_LT || ast_node->op == OPERATOR_LE || 
        ast_node->op == OPERATOR_GT || ast_node->op == OPERATOR_GE) {
        type_id = LMD_TYPE_BOOL;
    } else if (ast_node->op == OPERATOR_IDIV) {
        type_id = LMD_TYPE_INT;
    } else {
        type_id = LMD_TYPE_ANY;
    }
    ast_node->type = alloc_type(tp, type_id, sizeof(LambdaType));
    return (AstNode*)ast_node;
}

AstNode* build_if_expr(Transpiler* tp, TSNode if_node) {
    printf("build if expr\n");
    AstIfExprNode* ast_node = (AstIfExprNode*)alloc_ast_node(tp, AST_NODE_IF_EXPR, if_node, sizeof(AstIfExprNode));
    TSNode cond_node = ts_node_child_by_field_id(if_node, FIELD_COND);
    ast_node->cond = build_expr(tp, cond_node);
    TSNode then_node = ts_node_child_by_field_id(if_node, FIELD_THEN);
    ast_node->then = build_expr(tp, then_node);
    TSNode else_node = ts_node_child_by_field_id(if_node, FIELD_ELSE);
    ast_node->otherwise = build_expr(tp, else_node);
    // determine the type of the if expression
    ast_node->type = ast_node->then->type;
    return (AstNode*)ast_node;
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
        if (field_id == FIELD_DECLARE) {
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

    TSNode then_node = ts_node_child_by_field_id(let_node, FIELD_THEN);
    ast_node->then = build_expr(tp, then_node);
    if (!ast_node->then) { printf("missing let then\n"); }
    else { printf("got let then type %d\n", ast_node->then->node_type); }

    // determine let node type
    ast_node->type = ast_node->then->type;
    return (AstNode*)ast_node;
}

AstNode* build_const_stam(Transpiler* tp, TSNode let_node) {
    printf("build const stam\n");
    AstLetNode* ast_node = (AstLetNode*)alloc_ast_node(tp, AST_NODE_LET_STAM, let_node, sizeof(AstLetNode));

    // let can have multiple cond declarations
    TSTreeCursor cursor = ts_tree_cursor_new(let_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode *prev_declare = NULL;
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {
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
    if (!ast_node->declare) { printf("missing const declare\n"); }

    // const statement does not have 'then' clause
    return (AstNode*)ast_node;
}

void push_name(Transpiler* tp, AstNamedNode* node) {
    printf("pushing name %.*s\n", (int)node->name.length, node->name.str);
    NameEntry *entry = (NameEntry*)alloc_ast_bytes(tp, sizeof(NameEntry));
    entry->name = node->name;  entry->node = (AstNode*)node;
    if (!tp->current_scope->first) { tp->current_scope->first = entry; }
    if (!tp->current_scope->last) { tp->current_scope->last = entry; }
    else { tp->current_scope->last->next = entry; }
    tp->current_scope->last = entry;
}

AstNode* build_assign_expr(Transpiler* tp, TSNode asn_node) {
    printf("build assign expr\n");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_ASSIGN, asn_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(asn_node, FIELD_NAME);
    int start_byte = ts_node_start_byte(name);
    ast_node->name.str = tp->source + start_byte;
    ast_node->name.length = ts_node_end_byte(name) - start_byte;

    TSNode val_node = ts_node_child_by_field_id(asn_node, FIELD_THEN);
    ast_node->then = build_expr(tp, val_node);

    // determine the type of the variable
    ast_node->type = ast_node->then->type;

    // push the name to the name stack
    push_name(tp, ast_node);
    return (AstNode*)ast_node;
}

AstNamedNode* build_pair_expr(Transpiler* tp, TSNode pair_node) {
    printf("build pair expr\n");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_ASSIGN, pair_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(pair_node, FIELD_NAME);
    int start_byte = ts_node_start_byte(name);
    ast_node->name.str = tp->source + start_byte;
    ast_node->name.length = ts_node_end_byte(name) - start_byte;

    TSNode val_node = ts_node_child_by_field_id(pair_node, FIELD_THEN);
    printf("build pair then\n");
    ast_node->then = build_expr(tp, val_node);

    // determine the type of the field
    ast_node->type = ast_node->then->type;
    return ast_node;
}

AstNode* build_map_expr(Transpiler* tp, TSNode map_node) {
    printf("build map expr\n");
    AstMapNode* ast_node = (AstMapNode*)alloc_ast_node(tp, AST_NODE_MAP, map_node, sizeof(AstMapNode));
    ast_node->type = alloc_type(tp, LMD_TYPE_MAP, sizeof(LambdaTypeMap));
    LambdaTypeMap *type = (LambdaTypeMap*)ast_node->type;

    TSNode child = ts_node_named_child(map_node, 0);
    AstNamedNode* prev_item = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    while (!ts_node_is_null(child)) {
        AstNamedNode* item = build_pair_expr(tp, child);
        if (!prev_item) { ast_node->item = item; } 
        else { prev_item->next = (AstNode*)item; }
        prev_item = item;

        ShapeEntry* shape_entry = (ShapeEntry*)alloc_ast_bytes(tp, sizeof(ShapeEntry));
        shape_entry->name = item->name;
        shape_entry->type = item->type;
        shape_entry->byte_offset = byte_offset;
        if (!prev_entry) {
            type->shape = shape_entry;
        } else {
            prev_entry->next = shape_entry;
        }
        prev_entry = shape_entry;

        type->length++;
        byte_offset += byte_size[item->type->type_id];
        child = ts_node_next_named_sibling(child);
    }

    arraylist_append(tp->type_list, ast_node);
    type->type_index = tp->type_list->length - 1;
    type->byte_size = byte_offset;
    return (AstNode*)ast_node;
}

AstNode* build_loop_expr(Transpiler* tp, TSNode loop_node) {
    printf("build loop expr\n");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_LOOP, loop_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(loop_node, FIELD_NAME);
    int start_byte = ts_node_start_byte(name);
    ast_node->name.str = tp->source + start_byte;
    ast_node->name.length = ts_node_end_byte(name) - start_byte;

    TSNode expr_node = ts_node_child_by_field_id(loop_node, FIELD_THEN);
    ast_node->then = build_expr(tp, expr_node);

    // determine the type of the variable
    ast_node->type = ast_node->then->type;

    // push the name to the name stack
    push_name(tp, ast_node);
    return (AstNode*)ast_node;
}

AstNode* build_for_expr(Transpiler* tp, TSNode for_node) {
    printf("build for expr\n");
    AstForNode* ast_node = (AstForNode*)alloc_ast_node(tp, AST_NODE_FOR_EXPR, for_node, sizeof(AstForNode));

    // for can have multiple loop declarations
    TSTreeCursor cursor = ts_tree_cursor_new(for_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode *prev_loop = NULL;
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode *loop = build_loop_expr(tp, child);
            printf("got loop type %d\n", loop->node_type);
            if (prev_loop == NULL) {
                ast_node->loop = loop;
            } else {
                prev_loop->next = loop;
            }
            prev_loop = loop;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    if (!ast_node->loop) { printf("missing for loop declare\n"); }

    TSNode then_node = ts_node_child_by_field_id(for_node, FIELD_THEN);
    ast_node->then = build_expr(tp, then_node);
    if (!ast_node->then) { printf("missing for then\n"); }
    else { printf("got for then type %d\n", ast_node->then->node_type); }

    // determine for node type
    ast_node->type = ast_node->then->type;
    return (AstNode*)ast_node;
}

AstNamedNode* build_param_expr(Transpiler* tp, TSNode param_node) {
    printf("build param expr\n");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_PARAM, param_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(param_node, FIELD_NAME);
    int start_byte = ts_node_start_byte(name);
    ast_node->name.str = tp->source + start_byte;
    ast_node->name.length = ts_node_end_byte(name) - start_byte;

    // TSNode val_node = ts_node_child_by_field_id(param_node, FIELD_THEN);
    // printf("build param then\n");
    // ast_node->then = build_expr(tp, val_node);

    // determine the type of the field
    ast_node->type = &TYPE_ANY; // ast_node->then->type;
    return ast_node;
}

AstNode* build_func(Transpiler* tp, TSNode func_node) {
    printf("build function\n");
    AstFuncNode* ast_node = (AstFuncNode*)alloc_ast_node(tp, AST_NODE_FUNC, func_node, sizeof(AstFuncNode));
    ast_node->type = &TYPE_FUNC;
    // get the function name
    TSNode fn_name_node = ts_node_child_by_field_id(func_node, FIELD_NAME);
    ast_node->name = fn_name_node;
    // build the params
    // let can have multiple cond declarations
    TSTreeCursor cursor = ts_tree_cursor_new(func_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNamedNode *prev_param = NULL;
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNamedNode *param = build_param_expr(tp, child);
            printf("got param type %d\n", param->node_type);
            if (prev_param == NULL) {
                ast_node->param = param;
            } else {
                prev_param->next = (AstNode*)param;
            }
            prev_param = param;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);    

    // build the function body
    TSNode fn_body_node = ts_node_child_by_field_id(func_node, FIELD_BODY);
    ast_node->body = build_expr(tp, fn_body_node);    
    return (AstNode*)ast_node;
}

AstNode* build_content(Transpiler* tp, TSNode list_node) {
    printf("build content\n");
    AstArrayNode* ast_node = (AstArrayNode*)alloc_ast_node(tp, AST_NODE_LIST, list_node, sizeof(AstArrayNode));
    ast_node->type = alloc_type(tp, LMD_TYPE_LIST, sizeof(LambdaTypeArray));
    LambdaTypeArray *type = (LambdaTypeArray*)ast_node->type;
    TSNode child = ts_node_named_child(list_node, 0);
    AstNode* prev_item = NULL;
    while (!ts_node_is_null(child)) {
        AstNode* item = build_expr(tp, child);
        if (!prev_item) { 
            ast_node->item = item;
        } else {  
            prev_item->next = item;
        }
        prev_item = item;
        type->length++;
        child = ts_node_next_named_sibling(child);
    }
    return (AstNode*)ast_node;
}

AstNode* build_expr(Transpiler* tp, TSNode expr_node) {
    // get the function name
    TSSymbol symbol = ts_node_symbol(expr_node);
    if (symbol == SYM_IF_EXPR) {
        return build_if_expr(tp, expr_node);
    }
    else if (symbol == SYM_BINARY_EXPR) {
        return build_binary_expr(tp, expr_node);
    }
    else if (symbol == SYM_PRIMARY_EXPR) {
        return build_primary_expr(tp, expr_node);
    }
    else if (symbol == SYM_LET_EXPR) {
        return build_let_expr(tp, expr_node);
    }
    else if (symbol == SYM_CONST_STAM) {
        return build_const_stam(tp, expr_node);
    }
    else if (symbol == SYM_FOR_EXPR) {
        return build_for_expr(tp, expr_node);
    }
    else if (symbol == SYM_ASSIGN_EXPR) {
        return build_assign_expr(tp, expr_node);
    }  
    else if (symbol == SYM_ARRAY) {
        return build_array_expr(tp, expr_node);
    }
    else if (symbol == SYM_MAP) {
        return build_map_expr(tp, expr_node);
    }
    else if (symbol == SYM_IDENT) {
        return build_identifier(tp, expr_node);
    }
    else if (symbol == SYM_FUNC) {
        return build_func(tp, expr_node);
    }
    else if (symbol == SYM_CONTENT) {
        return build_content(tp, expr_node);
    }
    else {
        printf("unknown expr %s\n", ts_node_type(expr_node));
        return NULL;
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
    return (AstNode*)ast_node;
}

char* formatType(LambdaType *type) {
    if (!type) { return "null*"; }
    TypeId type_id = type->type_id;
    switch (type_id) {
    case LMD_TYPE_NULL:
        return "void*";
    case LMD_TYPE_ANY:
        return "any";
    case LMD_TYPE_ERROR:
        return "ERROR";        
    case LMD_TYPE_BOOL:
        return "bool";        
    case LMD_TYPE_INT:
        return "int";
    case LMD_TYPE_FLOAT:
        return "float";
    case LMD_TYPE_DOUBLE:
        return "double";
    case LMD_TYPE_STRING:
        return "char*";

    case LMD_TYPE_ARRAY:
        LambdaTypeArray *array_type = (LambdaTypeArray*)type;
        if (array_type->nested && array_type->nested->type_id == LMD_TYPE_INT) {
            return "ArrayInt*";
        } else {
            return "Array*";
        }
    case LMD_TYPE_LIST:
        return "List*";
    case LMD_TYPE_MAP:
        return "Map*";
    case LMD_TYPE_ELEMENT:
        return "Elmt*";
    case LMD_TYPE_FUNC:
        return "Func*";
    default:
        return "UNKNOWN";
    }
}

AstNode* print_ast_node(AstNode *node, int indent) {
    for (int i = 0; i < indent; i++) { printf("  "); }
    // get the function name
    switch(node->node_type) {
    case AST_NODE_IF_EXPR:
        printf("[if expr:%s]\n", formatType(node->type));
        print_ast_node(((AstIfExprNode*)node)->cond, indent + 1);
        print_ast_node(((AstIfExprNode*)node)->then, indent + 1);
        print_ast_node(((AstIfExprNode*)node)->otherwise, indent + 1);
        break;
    case AST_NODE_BINARY:
        printf("[binary expr:%s]\n", formatType(node->type));
        print_ast_node(((AstBinaryNode*)node)->left, indent + 1);
        print_ast_node(((AstBinaryNode*)node)->right, indent + 1);
        break;
    case AST_NODE_LET_EXPR:  case AST_NODE_LET_STAM:
        printf("[let %s:%s]\n", node->node_type == AST_NODE_LET_EXPR ? "expr" : "stam", formatType(node->type));
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
    case AST_NODE_FOR_EXPR:
        printf("[for %s:%s]\n", node->node_type == AST_NODE_FOR_EXPR ? "expr" : "stam", formatType(node->type));
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            for (int i = 0; i < indent+1; i++) { printf("  "); }
            printf("loop:\n");
            print_ast_node(loop, indent + 1);
            loop = loop->next;
        }
        if (node->node_type == AST_NODE_FOR_EXPR) {
            for (int i = 0; i < indent+1; i++) { printf("  "); }
            printf("then:\n");
            print_ast_node(((AstForNode*)node)->then, indent + 1);
        }
        break;
    case AST_NODE_ASSIGN:
        printf("[assign expr:%s]\n", formatType(node->type));
        print_ast_node(((AstNamedNode*)node)->then, indent + 1);
        break;
    case AST_NODE_LOOP:
        printf("[loop expr:%s]\n", formatType(node->type));
        print_ast_node(((AstNamedNode*)node)->then, indent + 1);
        break;
    case AST_NODE_ARRAY:
        printf("[array expr:%s]\n", formatType(node->type));
        AstNode *item = ((AstArrayNode*)node)->item;
        while (item) {
            for (int i = 0; i < indent+1; i++) { printf("  "); }
            printf("item:\n");
            print_ast_node(item, indent + 1);
            item = item->next;
        }        
        break;
    case AST_NODE_LIST:
        printf("[list expr:%s]\n", formatType(node->type));
        AstNode *li = ((AstArrayNode*)node)->item;
        while (li) {
            for (int i = 0; i < indent+1; i++) { printf("  "); }
            printf("item:\n");
            print_ast_node(li, indent + 1);
            li = li->next;
        }        
        break;        
    case AST_NODE_MAP:
        printf("[map expr:%s]\n", formatType(node->type));
        AstNamedNode *nm_item = ((AstMapNode*)node)->item;
        while (nm_item) {
            for (int i = 0; i < indent+1; i++) { printf("  "); }
            printf("item:\n");
            print_ast_node((AstNode*)nm_item, indent + 1);
            nm_item = (AstNamedNode*)nm_item->next;
        }
        break;        
    case AST_NODE_FIELD_EXPR:
        printf("[field expr:%s]\n", formatType(node->type));
        for (int i = 0; i < indent+1; i++) { printf("  "); }
        printf("object:\n");        
        print_ast_node(((AstFieldNode*)node)->object, indent + 1);
        for (int i = 0; i < indent+1; i++) { printf("  "); }
        printf("field:\n");        
        print_ast_node(((AstFieldNode*)node)->field, indent + 1);
        break;
    case AST_NODE_CALL_EXPR:
        printf("[call expr:%s]\n", formatType(node->type));
        print_ast_node(((AstCallNode*)node)->function, indent + 1);
        for (int i = 0; i < indent+1; i++) { printf("  "); }
        printf("args:\n"); 
        AstNode* arg = ((AstCallNode*)node)->argument;
        while (arg) {
            print_ast_node(arg, indent + 1);
            arg = arg->next;
        }
        break;
    case AST_NODE_FUNC:
        printf("[function expr:%s]\n", formatType(node->type));
        print_ast_node(((AstFuncNode*)node)->body, indent + 1);
        break;
    case AST_NODE_PRIMARY:
        printf("[primary expr:%s]\n", formatType(node->type));
        if (((AstPrimaryNode*)node)->expr) {
            print_ast_node(((AstPrimaryNode*)node)->expr, indent + 1);
        }
        break;
    case AST_NODE_IDENT:
        printf("[ident:%.*s:%s]\n", (int)((AstNamedNode*)node)->name.length, 
            ((AstNamedNode*)node)->name.str, formatType(node->type));
        break;
    case AST_SCRIPT:
        printf("[script:%s]\n", formatType(node->type));
        AstNode* child = ((AstScript*)node)->child;
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

