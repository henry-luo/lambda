#include "transpiler.h"
#include "../lib/hashmap.h"

LambdaType TYPE_ANY = {.type_id = LMD_TYPE_ANY};
LambdaType TYPE_ERROR = {.type_id = LMD_TYPE_ERROR};
LambdaType TYPE_BOOL = {.type_id = LMD_TYPE_BOOL};
LambdaType TYPE_IMP_INT = {.type_id = LMD_TYPE_IMP_INT};
LambdaType TYPE_INT = {.type_id = LMD_TYPE_INT};
LambdaType TYPE_FLOAT = {.type_id = LMD_TYPE_FLOAT};
LambdaType TYPE_NUMBER = {.type_id = LMD_TYPE_NUMBER};
LambdaType TYPE_STRING = {.type_id = LMD_TYPE_STRING};
LambdaType TYPE_FUNC = {.type_id = LMD_TYPE_FUNC};

LambdaType CONST_BOOL = {.type_id = LMD_TYPE_BOOL, .is_const = 1};
LambdaType CONST_IMP_INT = {.type_id = LMD_TYPE_IMP_INT, .is_const = 1};
LambdaType CONST_FLOAT = {.type_id = LMD_TYPE_FLOAT, .is_const = 1};
LambdaType CONST_STRING = {.type_id = LMD_TYPE_STRING, .is_const = 1};

LambdaType LIT_NULL = {.type_id = LMD_TYPE_NULL, .is_const = 1, .is_literal = 1};
LambdaType LIT_BOOL = {.type_id = LMD_TYPE_BOOL, .is_const = 1, .is_literal = 1};
LambdaType LIT_IMP_INT = {.type_id = LMD_TYPE_IMP_INT, .is_const = 1, .is_literal = 1};
LambdaType LIT_FLOAT = {.type_id = LMD_TYPE_FLOAT, .is_const = 1, .is_literal = 1};
LambdaType LIT_STRING = {.type_id = LMD_TYPE_STRING, .is_const = 1, .is_literal = 1};
LambdaType LIT_TYPE = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1};

LambdaTypeType LIT_TYPE_INT = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_INT};
LambdaTypeType LIT_TYPE_FLOAT = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_FLOAT};
LambdaTypeType LIT_TYPE_STRING = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_STRING};

TypeInfo type_info[] = {
    [LMD_TYPE_RAW_POINTER] = {.byte_size = sizeof(void*), .name = "pointer"},
    [LMD_TYPE_NULL] = {.byte_size = sizeof(bool), .name = "null"},
    [LMD_TYPE_BOOL] = {.byte_size = sizeof(bool), .name = "bool"},
    [LMD_TYPE_IMP_INT] = {.byte_size = sizeof(long), .name = "number"},
    [LMD_TYPE_INT] = {.byte_size = sizeof(long), .name = "int"},
    [LMD_TYPE_FLOAT] = {.byte_size = sizeof(double), .name = "float"},
    [LMD_TYPE_DECIMAL] = {.byte_size = sizeof(void*), .name = "decimal"},
    [LMD_TYPE_NUMBER] = {.byte_size = sizeof(double), .name = "number"},
    [LMD_TYPE_DTIME] = {.byte_size = sizeof(char*), .name = "datetime"},
    [LMD_TYPE_STRING] = {.byte_size = sizeof(char*), .name = "string"},
    [LMD_TYPE_SYMBOL] = {.byte_size = sizeof(char*), .name = "symbol"},
    [LMD_TYPE_BINARY] = {.byte_size = sizeof(char*), .name = "binary"},
    [LMD_TYPE_ARRAY] = {.byte_size = sizeof(void*), .name = "array"},
    [LMD_TYPE_ARRAY_INT] = {.byte_size = sizeof(void*), .name = "array"},
    [LMD_TYPE_LIST] = {.byte_size = sizeof(void*), .name = "list"},
    [LMD_TYPE_MAP] = {.byte_size = sizeof(void*), .name = "map"},
    [LMD_TYPE_ELEMENT] = {.byte_size = sizeof(void*), .name = "element"},
    [LMD_TYPE_TYPE] = {.byte_size = sizeof(void*), .name = "type"},
    [LMD_TYPE_FUNC] = {.byte_size = sizeof(void*), .name = "function"},
    [LMD_TYPE_ANY] = {.byte_size = sizeof(void*), .name = "any"},
    [LMD_TYPE_ERROR] = {.byte_size = sizeof(void*), .name = "error"},
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
    if (ast_node->function->type->type_id == LMD_TYPE_FUNC) {
        ast_node->type = ((LambdaTypeFunc*)ast_node->function->type)->returned;
        if (!ast_node->type) { // e.g. recursive fn
            ast_node->type = &TYPE_ANY;
        }
    } else {
        ast_node->type = &TYPE_ANY;
    }

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
    printf("end building call expr\n");
    return (AstNode*)ast_node;
}

NameEntry *lookup_name(Transpiler* tp, StrView var_name) {
    // lookup the name
    NameScope *scope = tp->current_scope;
    FIND_VAR_NAME:
    NameEntry *entry = scope->first;
    while (entry) {
        printf("checking name: %.*s vs. %.*s\n", 
            (int)entry->name.length, entry->name.str, (int)var_name.length, var_name.str);
        if (strview_eq(&entry->name, &var_name)) { break; }
        entry = entry->next;
    }
    if (!entry) {
        if (scope->parent) {
            assert(scope != scope->parent);
            scope = scope->parent;
            printf("checking parent scope: %p\n", scope);
            goto FIND_VAR_NAME;
        }
        printf("missing identifier %.*s\n", (int)var_name.length, var_name.str);
        return NULL;
    }
    else {
        printf("found identifier %.*s\n", (int)entry->name.length, entry->name.str);
        return entry;
    }
}

AstNode* build_identifier(Transpiler* tp, TSNode id_node) {
    printf("building identifier\n");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_IDENT, id_node, sizeof(AstNamedNode));

    // get the identifier name
    StrView var_name = ts_node_source(tp, id_node);
    ast_node->name = var_name;
    
    // lookup the name
    printf("looking up name: %.*s\n", (int)var_name.length, var_name.str);
    NameEntry *entry = lookup_name(tp, var_name);
    if (!entry) {
        ast_node->type = &TYPE_ERROR;
    } else {
        printf("found identifier %.*s\n", (int)entry->name.length, entry->name.str);
        ast_node->as = entry->node;
        ast_node->type = entry->node->type;
    }
    return (AstNode*)ast_node;
}

LambdaType* build_lit_string(Transpiler* tp, TSNode node) {
    TSSymbol symbol = ts_node_symbol(node);
    // todo: exclude zero-length string
    int start = ts_node_start_byte(node), end = ts_node_end_byte(node);
    int len =  end - start - (symbol == SYM_DATETIME || symbol == SYM_TIME ? 0 : symbol == SYM_BINARY ? 3:2);  // -2 to exclude the quotes
    LambdaTypeString *str_type = (LambdaTypeString*)alloc_type(tp, 
        (symbol == SYM_DATETIME || symbol == SYM_TIME) ? LMD_TYPE_DTIME :
        symbol == SYM_STRING ? LMD_TYPE_STRING : 
        symbol == SYM_BINARY ? LMD_TYPE_BINARY : 
        LMD_TYPE_SYMBOL, sizeof(LambdaTypeString));
    str_type->is_const = 1;  str_type->is_literal = 1;
    // copy the string, todo: handle escape sequence
    pool_variable_alloc(tp->ast_pool, sizeof(String) + len + 1, (void **)&str_type->string);
    const char* str_content = tp->source + start + 
        (symbol == SYM_DATETIME || symbol == SYM_TIME ? 0: symbol == SYM_BINARY ? 2:1);
    memcpy(str_type->string->str, str_content, len);  // memcpy is probably faster than strcpy
    str_type->string->str[len] = '\0';
    str_type->string->len = len;
    // add to const list
    arraylist_append(tp->const_list, str_type->string);
    str_type->const_index = tp->const_list->length - 1;
    printf("const string: %p, len %d, index %d\n", str_type->string, len, str_type->const_index);
    // ast_node->type = (LambdaType *)str_type;
    return (LambdaType *)str_type;
}

LambdaType* build_lit_float(Transpiler* tp, TSNode node, TSSymbol symbol) {
    LambdaTypeItem *item_type = (LambdaTypeItem *)alloc_type(tp, LMD_TYPE_FLOAT, sizeof(LambdaTypeItem));
    if (symbol == SYM_INF) {
        item_type->double_val = INFINITY;
    }
    else if (symbol == SYM_NAN) {
        item_type->double_val = NAN;
    }
    else {
        const char* num_str = tp->source + ts_node_start_byte(node);
        item_type->double_val = atof(num_str);
    }
    arraylist_append(tp->const_list, &item_type->double_val);
    item_type->const_index = tp->const_list->length - 1;
    item_type->is_const = 1;  item_type->is_literal = 1;
    // ast_node->type = (LambdaType *)item_type;
    return (LambdaType *)item_type;
}

AstNode* build_primary_expr(Transpiler* tp, TSNode pri_node) {
    printf("build primary expr\n");
    AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, pri_node, sizeof(AstPrimaryNode));
    TSNode child = ts_node_named_child(pri_node, 0);
    if (ts_node_is_null(child)) { return (AstNode*)ast_node; }

    // infer data type
    TSSymbol symbol = ts_node_symbol(child);
    if (symbol == SYM_NULL) {
        ast_node->type = &LIT_NULL;
    }
    else if (symbol == SYM_TRUE || symbol == SYM_FALSE) {
        ast_node->type = &LIT_BOOL;
    }
    else if (symbol == SYM_INT) {
        ast_node->type = &LIT_IMP_INT;
    }
    else if (symbol == SYM_FLOAT || symbol == SYM_INF || symbol == SYM_NAN) {
        ast_node->type = build_lit_float(tp, child, symbol);
    }
    else if (symbol == SYM_STRING || symbol == SYM_SYMBOL || 
        symbol == SYM_DATETIME || symbol == SYM_TIME || symbol == SYM_BINARY) {
        ast_node->type = build_lit_string(tp, child);
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
    printf("end build primary expr\n");
    return (AstNode*)ast_node;
}

AstNode* build_unary_expr(Transpiler* tp, TSNode bi_node) {
    printf("build unary expr\n");
    AstUnaryNode* ast_node = (AstUnaryNode*)alloc_ast_node(tp, AST_NODE_UNARY, bi_node, sizeof(AstUnaryNode));
    TSNode op_node = ts_node_child_by_field_id(bi_node, FIELD_OPERATOR);
    StrView op = ts_node_source(tp, op_node);
    ast_node->operator = op;
    if (strview_equal(&op, "not")) { ast_node->op = OPERATOR_NOT; }
    else if (strview_equal(&op, "-")) { ast_node->op = OPERATOR_NEG; }
    else if (strview_equal(&op, "+")) { ast_node->op = OPERATOR_POS; }

    TSNode operand_node = ts_node_child_by_field_id(bi_node, FIELD_OPERAND);
    ast_node->operand = build_expr(tp, operand_node);
    // ast_node->type = alloc_type(tp, type_id, sizeof(LambdaType));
    ast_node->type = ast_node->op == OPERATOR_NOT ? &TYPE_BOOL : &TYPE_FLOAT;

    printf("end build unary expr\n");
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
    else if (strview_equal(&op, "to")) { ast_node->op = OPERATOR_TO; }
    else if (strview_equal(&op, "union")) { ast_node->op = OPERATOR_UNION; }
    else if (strview_equal(&op, "intersect")) { ast_node->op = OPERATOR_INTERSECT; }
    else if (strview_equal(&op, "exclude")) { ast_node->op = OPERATOR_EXCLUDE; }
    else if (strview_equal(&op, "is")) { ast_node->op = OPERATOR_IS; }
    else if (strview_equal(&op, "in")) { ast_node->op = OPERATOR_IN; }
    else { printf("unknown operator: %.*s\n", (int)op.length, op.str); }

    TSNode right_node = ts_node_child_by_field_id(bi_node, FIELD_RIGHT);
    ast_node->right = build_expr(tp, right_node);

    printf("get binary type\n");
    printf("left type: %d, right type: %d\n", ast_node->left->type->type_id, ast_node->right->type->type_id);
    TypeId type_id;
    if (ast_node->op == OPERATOR_DIV || ast_node->op == OPERATOR_POW) {
        type_id = LMD_TYPE_FLOAT;
    } else if (ast_node->op == OPERATOR_ADD || ast_node->op == OPERATOR_SUB || 
        ast_node->op == OPERATOR_MUL || ast_node->op == OPERATOR_MOD) {
        type_id = max(ast_node->left->type->type_id, ast_node->right->type->type_id);
    } else if (ast_node->op == OPERATOR_AND || ast_node->op == OPERATOR_OR || 
        ast_node->op == OPERATOR_EQ || ast_node->op == OPERATOR_NE || 
        ast_node->op == OPERATOR_LT || ast_node->op == OPERATOR_LE || 
        ast_node->op == OPERATOR_GT || ast_node->op == OPERATOR_GE) {
        type_id = LMD_TYPE_BOOL;
    } else if (ast_node->op == OPERATOR_IDIV) {
        type_id = LMD_TYPE_IMP_INT;
    } else {
        type_id = LMD_TYPE_ANY;
    }
    ast_node->type = alloc_type(tp, type_id, sizeof(LambdaType));
    printf("end build binary expr\n");
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
    if (ts_node_is_null(else_node)) {
        ast_node->otherwise = NULL;
    } else {
        ast_node->otherwise = build_expr(tp, else_node);
    }
    // determine the type of the if expression, should be union of then and else
    TypeId type_id = max(ast_node->then->type->type_id, 
        ast_node->otherwise ? ast_node->otherwise->type->type_id : LMD_TYPE_NULL);
    ast_node->type = alloc_type(tp, type_id, sizeof(LambdaType));
    printf("end build if expr\n");
    return (AstNode*)ast_node;
}

AstNode* build_list(Transpiler* tp, TSNode list_node) {
    printf("build list\n");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_LIST, list_node, sizeof(AstListNode));
    ast_node->type = alloc_type(tp, LMD_TYPE_LIST, sizeof(LambdaTypeList));
    LambdaTypeList *type = (LambdaTypeList*)ast_node->type;

    ast_node->vars = (NameScope*)alloc_ast_bytes(tp, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    tp->current_scope = ast_node->vars;

    TSNode child = ts_node_named_child(list_node, 0);
    AstNode *prev_declare = NULL, *prev_item = NULL;
    while (!ts_node_is_null(child)) {
        AstNode* item = build_expr(tp, child);
        if (item) {
            if (item->node_type == AST_NODE_ASSIGN) {
                AstNode *declare = item;
                printf("got declare type %d\n", declare->node_type);
                if (prev_declare == NULL) {
                    ast_node->declare = declare;
                } else {
                    prev_declare->next = declare;
                }
                prev_declare = declare;
            }
            else { // normal list item
                if (!prev_item) { 
                    ast_node->item = item;
                } else {  
                    prev_item->next = item;
                }
                prev_item = item;
                type->length++;   
            }
        }
        child = ts_node_next_named_sibling(child);
    }
    // determine the list type; handle special case of one item list
    // ast_node->type = ast_node->then->type;
    tp->current_scope = ast_node->vars->parent;
    return (AstNode*)ast_node;
}

AstNode* build_let_stam(Transpiler* tp, TSNode let_node) {
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

    // let statement does not have 'then' clause
    ast_node->type = &LIT_NULL;
    return (AstNode*)ast_node;
}

void push_name(Transpiler* tp, AstNamedNode* node) {
    printf("pushing name %.*s\n", (int)node->name.length, node->name.str);
    NameEntry *entry = (NameEntry*)alloc_ast_bytes(tp, sizeof(NameEntry));
    entry->name = node->name;  entry->node = (AstNode*)node;
    if (!tp->current_scope->first) { tp->current_scope->first = entry; }
    if (tp->current_scope->last) { tp->current_scope->last->next = entry; }
    tp->current_scope->last = entry;
}

LambdaType build_type_annotation(Transpiler* tp, TSNode type_node) {
    printf("build type annotation\n");
    LambdaType type;  memset(&type, 0, sizeof(LambdaType));
    StrView type_name = ts_node_source(tp, type_node);
    if (strview_equal(&type_name, "null")) {
        type.type_id = LMD_TYPE_NULL;
    }
    else if (strview_equal(&type_name, "error")) {
        type.type_id = LMD_TYPE_ERROR;
    }
    else if (strview_equal(&type_name, "any")) {
        type.type_id = LMD_TYPE_ANY;
    }       
    else if (strview_equal(&type_name, "int")) {
        type.type_id = LMD_TYPE_INT;
    }
    else if (strview_equal(&type_name, "float")) {
        type.type_id = LMD_TYPE_FLOAT;
    }
    else if (strview_equal(&type_name, "number")) {
        type.type_id = LMD_TYPE_NUMBER;
    }    
    else if (strview_equal(&type_name, "string")) {
        type.type_id = LMD_TYPE_STRING;
    }
    else if (strview_equal(&type_name, "symbol")) {
        type.type_id = LMD_TYPE_SYMBOL;
    }    
    else if (strview_equal(&type_name, "boolean")) {
        type.type_id = LMD_TYPE_BOOL;
    }
    else if (strview_equal(&type_name, "list")) {
        type.type_id = LMD_TYPE_LIST;
    }    
    else if (strview_equal(&type_name, "array")) {
        type.type_id = LMD_TYPE_ARRAY;
    }
    else if (strview_equal(&type_name, "map")) {
        type.type_id = LMD_TYPE_MAP;
    }
    else if (strview_equal(&type_name, "function")) {
        type.type_id = LMD_TYPE_FUNC;
    }
    else if (strview_equal(&type_name, "datetime")) {
        type.type_id = LMD_TYPE_DTIME;
    }
    else if (strview_equal(&type_name, "time")) {
        type.type_id = LMD_TYPE_DTIME;
    }
    else if (strview_equal(&type_name, "date")) {
        type.type_id = LMD_TYPE_DTIME;
    }
    else {
        printf("unknown type %.*s\n", (int)type_name.length, type_name.str);
        type.type_id = LMD_TYPE_ERROR;
    }
    return type;
}

AstNode* build_type_annote(Transpiler* tp, TSNode type_node) {
    AstTypeNode* ast_node = (AstTypeNode*)alloc_ast_node(tp, AST_NODE_TYPE, type_node, sizeof(AstTypeNode));
    ast_node->type = &LIT_TYPE;
    return (AstNode*)ast_node;
}

AstNode* build_assign_expr(Transpiler* tp, TSNode asn_node) {
    printf("build assign expr\n");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_ASSIGN, asn_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(asn_node, FIELD_NAME);
    int start_byte = ts_node_start_byte(name);
    ast_node->name.str = tp->source + start_byte;
    ast_node->name.length = ts_node_end_byte(name) - start_byte;

    TSNode type_node = ts_node_child_by_field_id(asn_node, FIELD_TYPE);

    TSNode val_node = ts_node_child_by_field_id(asn_node, FIELD_AS);
    ast_node->as = build_expr(tp, val_node);

    // determine the type of the variable
    if (ts_node_is_null(type_node)) {
        ast_node->type = ast_node->as->type;
    } else {
        ast_node->type = alloc_type(tp, LMD_TYPE_ANY, sizeof(LambdaType));
        *ast_node->type = build_type_annotation(tp, type_node);
    }

    // push the name to the name stack
    push_name(tp, ast_node);
    return (AstNode*)ast_node;
}

AstNamedNode* build_key_expr(Transpiler* tp, TSNode pair_node) {
    printf("build key expr\n");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_KEY_EXPR, pair_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(pair_node, FIELD_NAME);
    int start_byte = ts_node_start_byte(name);
    ast_node->name.str = tp->source + start_byte;
    ast_node->name.length = ts_node_end_byte(name) - start_byte;

    TSNode val_node = ts_node_child_by_field_id(pair_node, FIELD_AS);
    printf("build key as\n");
    ast_node->as = build_expr(tp, val_node);

    // determine the type of the field
    ast_node->type = ast_node->as->type;
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
        AstNamedNode* item = build_key_expr(tp, child);
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
        byte_offset += type_info[item->type->type_id].byte_size;
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

    TSNode expr_node = ts_node_child_by_field_id(loop_node, FIELD_AS);
    ast_node->as = build_expr(tp, expr_node);

    // determine the type of the variable
    ast_node->type = ast_node->as->type->type_id == LMD_TYPE_ARRAY || ast_node->as->type->type_id == LMD_TYPE_LIST ?
        ((LambdaTypeArray*)ast_node->as->type)->nested : ast_node->as->type;

    // push the name to the name stack
    push_name(tp, ast_node);
    return (AstNode*)ast_node;
}

AstNode* build_for_expr(Transpiler* tp, TSNode for_node) {
    printf("build for expr\n");
    AstForNode* ast_node = (AstForNode*)alloc_ast_node(tp, AST_NODE_FOR_EXPR, for_node, sizeof(AstForNode));

    ast_node->vars = (NameScope*)alloc_ast_bytes(tp, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    tp->current_scope = ast_node->vars;
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
    tp->current_scope = ast_node->vars->parent;
    return (AstNode*)ast_node;
}

AstNamedNode* build_param_expr(Transpiler* tp, TSNode param_node) {
    printf("build param expr\n");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_PARAM, param_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(param_node, FIELD_NAME);
    StrView name_str = ts_node_source(tp, name);
    ast_node->name = name_str;

    TSNode type_node = ts_node_child_by_field_id(param_node, FIELD_TYPE);
    // determine the type of the field
    ast_node->type = alloc_type(tp, LMD_TYPE_ANY, sizeof(LambdaTypeParam));
    if (!ts_node_is_null(type_node)) {
        *ast_node->type = build_type_annotation(tp, type_node);
    } else {
        *ast_node->type = ast_node->as ? *ast_node->as->type : TYPE_ANY;
    }

    push_name(tp, ast_node);
    return ast_node;
}

AstNode* build_func(Transpiler* tp, TSNode func_node, bool is_named) {
    printf("build function\n");
    AstFuncNode* ast_node = (AstFuncNode*)alloc_ast_node(tp,
        is_named ? AST_NODE_FUNC : AST_NODE_FUNC_EXPR, func_node, sizeof(AstFuncNode));
    ast_node->type = alloc_type(tp, LMD_TYPE_FUNC, sizeof(LambdaTypeFunc));
    LambdaTypeFunc *fn_type = (LambdaTypeFunc*) ast_node->type;
    fn_type->is_anonymous = !is_named;
    
    // get the function name
    if (is_named) {
        TSNode fn_name_node = ts_node_child_by_field_id(func_node, FIELD_NAME);
        StrView name = ts_node_source(tp, fn_name_node);
        ast_node->name = name;
        // add fn name to current scope
        push_name(tp, (AstNamedNode*)ast_node);
    }

    // build the params
    ast_node->vars = (NameScope*)alloc_ast_bytes(tp, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    tp->current_scope = ast_node->vars;
    TSTreeCursor cursor = ts_tree_cursor_new(func_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNamedNode *prev_param = NULL;  int param_count = 0;
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {  // param declaration
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNamedNode *param = build_param_expr(tp, child);
            printf("got param type %d\n", param->node_type);
            if (prev_param == NULL) {
                ast_node->param = param;
                fn_type->param = (LambdaTypeParam*)param->type;
            } else {
                prev_param->next = (AstNode*)param;
                ((LambdaTypeParam*)prev_param->type)->next = (LambdaTypeParam*)param->type;
            }
            prev_param = param;  param_count++;
        }
        else if (field_id == FIELD_TYPE) {  // return type
            TSNode child = ts_tree_cursor_current_node(&cursor);
            LambdaType *type = (LambdaType*)alloc_type(tp, LMD_TYPE_ANY, sizeof(LambdaType));
            *type = build_type_annotation(tp, child);
            fn_type->returned = type;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    fn_type->param_count = param_count;

    // build the function body
    // ast_node->locals = (NameScope*)alloc_ast_bytes(tp, sizeof(NameScope));
    // ast_node->locals->parent = tp->current_scope;
    // tp->current_scope = ast_node->locals;
    TSNode fn_body_node = ts_node_child_by_field_id(func_node, FIELD_BODY);
    ast_node->body = build_expr(tp, fn_body_node);
    if (!fn_type->returned) fn_type->returned = ast_node->body->type;

    // restore parent namescope
    tp->current_scope = ast_node->vars->parent;
    printf("end building fn\n");
    return (AstNode*)ast_node;
}

AstNode* build_content(Transpiler* tp, TSNode list_node) {
    printf("build content\n");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_CONTENT, list_node, sizeof(AstListNode));
    ast_node->type = alloc_type(tp, LMD_TYPE_LIST, sizeof(LambdaTypeList));
    LambdaTypeList *type = (LambdaTypeList*)ast_node->type;
    TSNode child = ts_node_named_child(list_node, 0);
    AstNode* prev_item = NULL;
    while (!ts_node_is_null(child)) {
        AstNode* item = build_expr(tp, child);
        if (item) {
            if (!prev_item) { 
                ast_node->item = item;
            } else {  
                prev_item->next = item;
            }
            prev_item = item;
            type->length++;
        }
        // else comment or error
        child = ts_node_next_named_sibling(child);
    }
    printf("end building content item: %p, %d\n", ast_node->item, type->length);
    if (type->length == 1) { return ast_node->item;}
    return (AstNode*)ast_node;
}

AstNode* build_expr(Transpiler* tp, TSNode expr_node) {
    // get the function name
    TSSymbol symbol = ts_node_symbol(expr_node);
    if (symbol == SYM_PRIMARY_EXPR) {
        return build_primary_expr(tp, expr_node);
    }
    else if (symbol == SYM_UNARY_EXPR) {
        return build_unary_expr(tp, expr_node);
    }    
    else if (symbol == SYM_BINARY_EXPR) {
        return build_binary_expr(tp, expr_node);
    }
    else if (symbol == SYM_LET_STAM || symbol == SYM_TYPE_DEFINE) {
        return build_let_stam(tp, expr_node);
    }
    else if (symbol == SYM_FOR_EXPR) {
        return build_for_expr(tp, expr_node);
    }
    else if (symbol == SYM_FOR_STAM) {
        return build_for_expr(tp, expr_node);
    }    
    else if (symbol == SYM_IF_EXPR) {
        return build_if_expr(tp, expr_node);
    }    
    else if (symbol == SYM_IF_STAM) {
        return build_if_expr(tp, expr_node);
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
    else if (symbol == SYM_FUNC_STAM) {
        return build_func(tp, expr_node, true);
    }
    else if (symbol == SYM_FUNC_EXPR_STAM) {
        return build_func(tp, expr_node, true);
    }
    else if (symbol == SYM_FUNC_EXPR) {
        return build_func(tp, expr_node, false);
    }    
    else if (symbol == SYM_CONTENT) {
        return build_content(tp, expr_node);
    }
    else if (symbol == SYM_LIST) {
        return build_list(tp, expr_node);
    }
    else if (symbol == SYM_STRING || symbol == SYM_SYMBOL || 
        symbol == SYM_DATETIME || symbol == SYM_TIME || symbol == SYM_BINARY) {
        AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        ast_node->type = build_lit_string(tp, expr_node);
        return (AstNode*)ast_node;
    }
    else if (symbol == SYM_TRUE || symbol == SYM_FALSE) {
        AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        ast_node->type = &LIT_BOOL;
        return (AstNode*)ast_node;
    }
    else if (symbol == SYM_INT) {
        AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        ast_node->type = &LIT_IMP_INT;
        return (AstNode*)ast_node;
    }
    else if (symbol == SYM_FLOAT) {
        AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        ast_node->type = build_lit_float(tp, expr_node, symbol);
        return (AstNode*)ast_node;
    }
    else if (symbol == SYM_BASE_TYPE) {
        AstTypeNode* ast_node = (AstTypeNode*)alloc_ast_node(tp, AST_NODE_TYPE, expr_node, sizeof(AstTypeNode));
        ast_node->type = &LIT_TYPE;
        return (AstNode*)ast_node;
    }
    else if (symbol == SYM_TYPE_ANNOTE) {
        return build_type_annote(tp, expr_node);
    }
    else if (symbol == SYM_COMMENT) {
        return NULL;
    }
    else {
        printf("unknown syntax node: %s\n", ts_node_type(expr_node));
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
        if (ast) {
            if (!prev) ast_node->child = ast;
            else { prev->next = ast; }
            prev = ast;
        }
        child = ts_node_next_named_sibling(child);
    }
    if (ast_node->child) ast_node->type = ast_node->child->type;
    printf("build script child: %p\n", ast_node->child);
    return (AstNode*)ast_node;
}
