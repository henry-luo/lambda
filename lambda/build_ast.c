#include "transpiler.h"
#include "../lib/hashmap.h"

Type TYPE_NULL = {.type_id = LMD_TYPE_NULL};
Type TYPE_BOOL = {.type_id = LMD_TYPE_BOOL};
Type TYPE_INT = {.type_id = LMD_TYPE_INT};
Type TYPE_INT64 = {.type_id = LMD_TYPE_INT64};
Type TYPE_FLOAT = {.type_id = LMD_TYPE_FLOAT};
Type TYPE_DECIMAL = {.type_id = LMD_TYPE_DECIMAL};
Type TYPE_NUMBER = {.type_id = LMD_TYPE_NUMBER};
Type TYPE_STRING = {.type_id = LMD_TYPE_STRING};
Type TYPE_BINARY = {.type_id = LMD_TYPE_BINARY};
Type TYPE_SYMBOL = {.type_id = LMD_TYPE_SYMBOL};
Type TYPE_DTIME = {.type_id = LMD_TYPE_DTIME};
Type TYPE_LIST = {.type_id = LMD_TYPE_LIST};
Type TYPE_RANGE = {.type_id = LMD_TYPE_RANGE};
TypeArray TYPE_ARRAY = {.type_id = LMD_TYPE_ARRAY};
Type TYPE_MAP = {.type_id = LMD_TYPE_MAP};
Type TYPE_ELMT = {.type_id = LMD_TYPE_ELEMENT};
Type TYPE_TYPE = {.type_id = LMD_TYPE_TYPE};
Type TYPE_FUNC = {.type_id = LMD_TYPE_FUNC};
Type TYPE_ANY = {.type_id = LMD_TYPE_ANY};
Type TYPE_ERROR = {.type_id = LMD_TYPE_ERROR};

Type CONST_BOOL = {.type_id = LMD_TYPE_BOOL, .is_const = 1};
Type CONST_INT = {.type_id = LMD_TYPE_INT, .is_const = 1};
Type CONST_FLOAT = {.type_id = LMD_TYPE_FLOAT, .is_const = 1};
Type CONST_STRING = {.type_id = LMD_TYPE_STRING, .is_const = 1};

Type LIT_NULL = {.type_id = LMD_TYPE_NULL, .is_const = 1, .is_literal = 1};
Type LIT_BOOL = {.type_id = LMD_TYPE_BOOL, .is_const = 1, .is_literal = 1};
Type LIT_INT = {.type_id = LMD_TYPE_INT, .is_const = 1, .is_literal = 1};
Type LIT_FLOAT = {.type_id = LMD_TYPE_FLOAT, .is_const = 1, .is_literal = 1};
Type LIT_DECIMAL = {.type_id = LMD_TYPE_DECIMAL, .is_const = 1, .is_literal = 1};
Type LIT_STRING = {.type_id = LMD_TYPE_STRING, .is_const = 1, .is_literal = 1};
Type LIT_TYPE = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1};

TypeType LIT_TYPE_NULL = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_NULL};
TypeType LIT_TYPE_BOOL = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_BOOL};
TypeType LIT_TYPE_INT = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_INT64};
TypeType LIT_TYPE_FLOAT = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_FLOAT};
TypeType LIT_TYPE_DECIMAL = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_DECIMAL};
TypeType LIT_TYPE_NUMBER = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_NUMBER};
TypeType LIT_TYPE_STRING = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_STRING};
TypeType LIT_TYPE_BINARY = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_BINARY};
TypeType LIT_TYPE_SYMBOL = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_SYMBOL};
TypeType LIT_TYPE_DTIME = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_DTIME};
TypeType LIT_TYPE_LIST = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_LIST};
TypeType LIT_TYPE_RANGE = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_RANGE};
TypeType LIT_TYPE_ARRAY = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = (Type*)&TYPE_ARRAY};
TypeType LIT_TYPE_MAP = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_MAP};
TypeType LIT_TYPE_ELMT = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_ELMT};
TypeType LIT_TYPE_FUNC = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_FUNC};
TypeType LIT_TYPE_TYPE = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_TYPE};
TypeType LIT_TYPE_ANY = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_ANY};
TypeType LIT_TYPE_ERROR = {.type_id = LMD_TYPE_TYPE, .is_const = 1, .is_literal = 1, .type = &TYPE_ERROR};

String EMPTY_STRING = {.chars = "lambda.nil", .len = sizeof("lambda.nil") - 1, .ref_cnt = 0};

TypeInfo type_info[] = {
    [LMD_TYPE_RAW_POINTER] = {.byte_size = sizeof(void*), .name = "pointer", .type = &TYPE_NULL, .lit_type = (Type*)&LIT_TYPE_NULL},
    [LMD_TYPE_NULL] = {.byte_size = sizeof(bool), .name = "null", .type=&TYPE_NULL, .lit_type = (Type*)&LIT_TYPE_NULL},
    [LMD_TYPE_BOOL] = {.byte_size = sizeof(bool), .name = "bool", .type=&TYPE_BOOL, .lit_type = (Type*)&LIT_TYPE_BOOL},
    [LMD_TYPE_INT] = {.byte_size = sizeof(long), .name = "int", .type=&TYPE_INT, .lit_type = (Type*)&LIT_TYPE_INT},
    [LMD_TYPE_INT64] = {.byte_size = sizeof(long), .name = "int", .type=&TYPE_INT, .lit_type = (Type*)&LIT_TYPE_INT},
    [LMD_TYPE_FLOAT] = {.byte_size = sizeof(double), .name = "float", .type=&TYPE_FLOAT, .lit_type = (Type*)&LIT_TYPE_FLOAT},
    [LMD_TYPE_DECIMAL] = {.byte_size = sizeof(void*), .name = "decimal", .type=&TYPE_DECIMAL, .lit_type = (Type*)&LIT_TYPE_DECIMAL},
    [LMD_TYPE_NUMBER] = {.byte_size = sizeof(double), .name = "number", .type=&TYPE_NUMBER, .lit_type = (Type*)&LIT_TYPE_NUMBER},
    [LMD_TYPE_DTIME] = {.byte_size = sizeof(char*), .name = "datetime", .type=&TYPE_DTIME, .lit_type = (Type*)&LIT_TYPE_DTIME},
    [LMD_TYPE_STRING] = {.byte_size = sizeof(char*), .name = "string", .type=&TYPE_STRING, .lit_type = (Type*)&LIT_TYPE_STRING},
    [LMD_TYPE_SYMBOL] = {.byte_size = sizeof(char*), .name = "symbol", .type=&TYPE_SYMBOL, .lit_type = (Type*)&LIT_TYPE_SYMBOL},
    [LMD_TYPE_BINARY] = {.byte_size = sizeof(char*), .name = "binary", .type=&TYPE_BINARY, .lit_type = (Type*)&LIT_TYPE_BINARY},
    [LMD_TYPE_LIST] = {.byte_size = sizeof(void*), .name = "list", .type=&TYPE_LIST, .lit_type = (Type*)&LIT_TYPE_LIST},
    [LMD_TYPE_RANGE] = {.byte_size = sizeof(void*), .name = "array", .type=&TYPE_RANGE, .lit_type = (Type*)&LIT_TYPE_RANGE},
    [LMD_TYPE_ARRAY] = {.byte_size = sizeof(void*), .name = "array", .type=(Type*)&TYPE_ARRAY, .lit_type = (Type*)&LIT_TYPE_ARRAY},
    [LMD_TYPE_ARRAY_INT] = {.byte_size = sizeof(void*), .name = "array", .type=(Type*)&TYPE_ARRAY, .lit_type = (Type*)&LIT_TYPE_ARRAY},
    [LMD_TYPE_MAP] = {.byte_size = sizeof(void*), .name = "map", .type=&TYPE_MAP, .lit_type = (Type*)&LIT_TYPE_MAP},
    [LMD_TYPE_ELEMENT] = {.byte_size = sizeof(void*), .name = "element", .type=&TYPE_ELMT, .lit_type = (Type*)&LIT_TYPE_ELMT},
    [LMD_TYPE_TYPE] = {.byte_size = sizeof(void*), .name = "type", .type=&TYPE_TYPE, .lit_type = (Type*)&LIT_TYPE_TYPE},
    [LMD_TYPE_FUNC] = {.byte_size = sizeof(void*), .name = "function", .type=&TYPE_FUNC, .lit_type = (Type*)&LIT_TYPE_FUNC},
    [LMD_TYPE_ANY] = {.byte_size = sizeof(void*), .name = "any", .type=&TYPE_ANY, .lit_type = (Type*)&LIT_TYPE_ANY},
    [LMD_TYPE_ERROR] = {.byte_size = sizeof(void*), .name = "error", .type=&TYPE_ERROR, .lit_type = (Type*)&LIT_TYPE_ERROR},
    [LMD_CONTAINER_HEAP_START] = {.byte_size = 0, .name = "container_start", .type=&TYPE_NULL, .lit_type = (Type*)&LIT_TYPE_NULL},
};

AstNamedNode* build_param_expr(Transpiler* tp, TSNode param_node, bool is_type);

AstNode* alloc_ast_node(Transpiler* tp, AstNodeType node_type, TSNode node, size_t size) {
    AstNode* ast_node;
    pool_variable_alloc(tp->ast_pool, size, (void**)&ast_node);
    memset(ast_node, 0, size);
    ast_node->node_type = node_type;  ast_node->node = node;
    return ast_node;
}

void* alloc_const(Transpiler* tp, size_t size) {
    void* bytes;
    pool_variable_alloc(tp->ast_pool, size, &bytes);
    memset(bytes, 0, size);
    return bytes;
}

Type* alloc_type(VariableMemPool* pool, TypeId type, size_t size) {
    Type* t;
    pool_variable_alloc(pool, size, (void**)&t);
    memset(t, 0, size);
    t->type_id = type;  assert(t->is_const == 0);
    return t;
}

void push_name(Transpiler* tp, AstNamedNode* node, AstImportNode* import) {
    printf("pushing name %.*s, %p\n", (int)node->name.length, node->name.str, node->type);
    NameEntry *entry = (NameEntry*)pool_calloc(tp->ast_pool, sizeof(NameEntry));
    entry->name = node->name;  
    entry->node = (AstNode*)node;  entry->import = import;
    if (!tp->current_scope->first) { tp->current_scope->first = entry; }
    if (tp->current_scope->last) { tp->current_scope->last->next = entry; }
    tp->current_scope->last = entry;
}

AstNode* build_array(Transpiler* tp, TSNode array_node) {
    printf("build array expr\n");
    AstArrayNode* ast_node = (AstArrayNode*)alloc_ast_node(tp, AST_NODE_ARRAY, array_node, sizeof(AstArrayNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
    TypeArray *type = (TypeArray*)ast_node->type;
    TSNode child = ts_node_named_child(array_node, 0);
    AstNode* prev_item = NULL;  Type *nested_type = NULL;
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

AstNode* build_field_expr(Transpiler* tp, TSNode array_node, AstNodeType node_type) {
    printf("build field expr\n");
    AstFieldNode* ast_node = (AstFieldNode*)alloc_ast_node(tp, node_type, array_node, sizeof(AstFieldNode));
    TSNode object_node = ts_node_child_by_field_id(array_node, FIELD_OBJECT);
    ast_node->object = build_expr(tp, object_node);

    TSNode field_node = ts_node_child_by_field_id(array_node, FIELD_FIELD);
    ast_node->field = build_expr(tp, field_node);

    if (ast_node->object->type->type_id == LMD_TYPE_ARRAY) {
        ast_node->type = ((TypeArray*)ast_node->object->type)->nested;
    }
    else if (ast_node->object->type->type_id == LMD_TYPE_MAP) {
        ast_node->type = &TYPE_ANY;  // todo: derive field type
    }
    else {
        ast_node->type = &TYPE_ANY;
    }
    return (AstNode*)ast_node;
}

AstNode* build_call_expr(Transpiler* tp, TSNode call_node, TSSymbol symbol) {
    printf("build call expr: %d\n", symbol);
    AstCallNode* ast_node = (AstCallNode*)alloc_ast_node(tp, 
        AST_NODE_CALL_EXPR, call_node, sizeof(AstCallNode));
    TSNode function_node = ts_node_child_by_field_id(call_node, FIELD_FUNCTION);
    if (symbol == SYM_SYS_FUNC) {
        printf("build sys call\n");
        AstSysFuncNode* fn_node = (AstSysFuncNode*)alloc_ast_node(tp, 
            AST_NODE_SYS_FUNC, function_node, sizeof(AstSysFuncNode));
        StrView func_name = ts_node_source(tp, function_node);
        if (strview_equal(&func_name, "len")) {
            fn_node->fn = SYSFUNC_LEN;
            fn_node->type = &TYPE_INT;
        }
        else if (strview_equal(&func_name, "type")) {
            fn_node->fn = SYSFUNC_TYPE;
            fn_node->type = &TYPE_TYPE;
        }
        else if (strview_equal(&func_name, "int")) {
            fn_node->fn = SYSFUNC_INT;
            fn_node->type = &TYPE_INT;
        }
        else if (strview_equal(&func_name, "float")) {
            fn_node->fn = SYSFUNC_FLOAT;
            fn_node->type = &TYPE_FLOAT;
        }
        else if (strview_equal(&func_name, "number")) {
            fn_node->fn = SYSFUNC_NUMBER;
            fn_node->type = &TYPE_NUMBER;
        }
        else if (strview_equal(&func_name, "string")) {
            fn_node->fn = SYSFUNC_STRING;
            fn_node->type = &TYPE_STRING;
        }
        else if (strview_equal(&func_name, "symbol")) {
            fn_node->fn = SYSFUNC_SYMBOL;
            fn_node->type = &TYPE_SYMBOL;
        }
        else if (strview_equal(&func_name, "datetime")) {
            fn_node->fn = SYSFUNC_DATETIME;
            fn_node->type = &TYPE_DTIME;
        }
        else if (strview_equal(&func_name, "date")) {
            fn_node->fn = SYSFUNC_DATE;
            fn_node->type = &TYPE_DTIME;
        }
        else if (strview_equal(&func_name, "time")) {
            fn_node->fn = SYSFUNC_TIME;
            fn_node->type = &TYPE_DTIME;
        }
        else if (strview_equal(&func_name, "input")) {
            fn_node->fn = SYSFUNC_INPUT;
            fn_node->type = &TYPE_ANY;
        }        
        else if (strview_equal(&func_name, "print")) {
            fn_node->fn = SYSFUNC_PRINT;
            fn_node->type = &TYPE_NULL;
        }
        else if (strview_equal(&func_name, "format")) {
            fn_node->fn = SYSFUNC_FORMAT;
            fn_node->type = &TYPE_STRING;
        }
        else if (strview_equal(&func_name, "error")) {
            fn_node->fn = SYSFUNC_ERROR;
            fn_node->type = &TYPE_ERROR;
        }
        else {
            printf("unknown system function: %.*s\n", (int)func_name.length, func_name.str);
            return NULL;
        }
        ast_node->function = (AstNode*)fn_node;
        ast_node->type = fn_node->type;
    }
    else {
        ast_node->function = build_expr(tp, function_node);
        if (ast_node->function->type->type_id == LMD_TYPE_FUNC) {
            ast_node->type = ((TypeFunc*)ast_node->function->type)->returned;
            if (!ast_node->type) { // e.g. recursive fn
                ast_node->type = &TYPE_ANY;
            }
        } else {
            ast_node->type = &TYPE_ANY;
        }
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
    printf("end building call expr type: %p\n", ast_node->type);
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
    AstIdentNode* ast_node = (AstIdentNode*)alloc_ast_node(tp, AST_NODE_IDENT, id_node, sizeof(AstIdentNode));

    // get the identifier name
    StrView var_name = ts_node_source(tp, id_node);
    ast_node->name = var_name;
    
    // lookup the name
    printf("looking up name: %.*s\n", (int)var_name.length, var_name.str);
    NameEntry *entry = lookup_name(tp, var_name);
    if (!entry) {
        // ident is used for member access, thus we return TYPE_ANY
        ast_node->type = &TYPE_ANY;
    } else {
        printf("found identifier %.*s\n", (int)entry->name.length, entry->name.str);
        ast_node->entry = entry;
        if (entry->import && entry->node->type->type_id != LMD_TYPE_FUNC) {
            // clone and remove is_const flag
            // todo: full type clone
            printf("got imported identifier %.*s from module %.*s\n", 
                (int)entry->name.length, entry->name.str, 
                (int)entry->import->module.length, entry->import->module.str);
            ast_node->type = alloc_type(tp->ast_pool, entry->node->type->type_id, sizeof(Type));
            assert(ast_node->type->is_const == 0);
        }
        else { ast_node->type = entry->node->type; }
        printf("ident %p type: %d\n", ast_node->type, ast_node->type ? ast_node->type->type_id : -1);
    }
    return (AstNode*)ast_node;
}

Type* build_lit_string(Transpiler* tp, TSNode node, TSSymbol symbol) {
    StrView sv = ts_node_source(tp, node);
    printf("build lit string: %.*s\n", (int)sv.length, sv.str);
    // todo: exclude zero-length string
    int start = ts_node_start_byte(node), end = ts_node_end_byte(node);
    int len =  end - start;
    TypeString *str_type = (TypeString*)alloc_type(tp->ast_pool, 
        (symbol == SYM_DATETIME || symbol == SYM_TIME) ? LMD_TYPE_DTIME :
        symbol == SYM_STRING ? LMD_TYPE_STRING : 
        symbol == SYM_BINARY ? LMD_TYPE_BINARY : 
        LMD_TYPE_SYMBOL, sizeof(TypeString));
    str_type->is_const = 1;  str_type->is_literal = 1;
    // copy the string, todo: handle escape sequence
    String *str;
    pool_variable_alloc(tp->ast_pool, sizeof(String) + len + 1, (void **)&str);
    str_type->string = (String*)str;
    const char* str_content = tp->source + start;
    memcpy(str->chars, str_content, len);  // memcpy is probably faster than strcpy
    str->chars[len] = '\0';  str->len = len;  str->ref_cnt = 1;
    printf("build lit string: %.*s, type: %d\n", 
        (int)str->len, str->chars, str_type->type_id);
    // add to const list
    arraylist_append(tp->const_list, str_type->string);
    str_type->const_index = tp->const_list->length - 1;
    return (Type *)str_type;
}

Type* build_lit_float(Transpiler* tp, TSNode node, TSSymbol symbol) {
    TypeFloat *item_type = (TypeFloat *)alloc_type(tp->ast_pool, LMD_TYPE_FLOAT, sizeof(TypeFloat));
    // C supports inf and nan
    printf("build lit float: normal\n");
    const char* num_str = tp->source + ts_node_start_byte(node);
    // check if there's sign
    bool has_sign = false;
    if (num_str[0] == '-') { has_sign = true;  num_str++; } // skip the sign
    // atof is able to skip leading spaces
    item_type->double_val = atof(num_str);
    if (has_sign) { item_type->double_val = -item_type->double_val; }
    arraylist_append(tp->const_list, &item_type->double_val);
    item_type->const_index = tp->const_list->length - 1;
    item_type->is_const = 1;  item_type->is_literal = 1;
    return (Type *)item_type;
}

Type* build_lit_decimal(Transpiler* tp, TSNode node) {
    TypeDecimal *item_type = (TypeDecimal *)alloc_type(tp->ast_pool, LMD_TYPE_DECIMAL, sizeof(TypeDecimal));
    StrView num_sv = ts_node_source(tp, node);
    char* num_str = strview_to_cstr(&num_sv);
    num_str[num_sv.length-1] = '\0';  // clear suffix 'n'/'N'
    mpf_init(item_type->dec_val);
    mpf_set_str(item_type->dec_val, num_str, 10);
    arraylist_append(tp->const_list, &item_type->dec_val);
    item_type->const_index = tp->const_list->length - 1;
    item_type->is_const = 1;  item_type->is_literal = 1;
    free(num_str);
    return (Type *)item_type;
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
        ast_node->type = &LIT_INT;  // todo: check int value range
    }
    else if (symbol == SYM_DECIMAL) {
        ast_node->type = build_lit_decimal(tp, child);
    }
    else if (symbol == SYM_FLOAT) {
        ast_node->type = build_lit_float(tp, child, symbol);
    }
    else if (symbol == SYM_STRING || symbol == SYM_SYMBOL || symbol == SYM_BINARY) {
        TSNode str_node = ts_node_named_child(child, 0);
        ast_node->type = build_lit_string(tp, str_node, symbol);
    }
    else if (symbol == SYM_DATETIME || symbol == SYM_TIME) {
        ast_node->type = build_lit_string(tp, child, symbol);
    }    
    else if (symbol == SYM_IDENT) {
        ast_node->expr = build_identifier(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_ARRAY) {
        ast_node->expr = build_array(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_MAP) {
        ast_node->expr = build_map(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_ELEMENT) {
        ast_node->expr = build_elmt(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_MEMBER_EXPR) {
        ast_node->expr = build_field_expr(tp, child, AST_NODE_MEMBER_EXPR);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_INDEX_EXPR) {
        ast_node->expr = build_field_expr(tp, child, AST_NODE_INDEX_EXPR);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_CALL_EXPR || symbol == SYM_SYS_FUNC) {
        ast_node->expr = build_call_expr(tp, child, symbol);
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
    ast_node->op_str = op;
    if (strview_equal(&op, "not")) { ast_node->op = OPERATOR_NOT; }
    else if (strview_equal(&op, "-")) { ast_node->op = OPERATOR_NEG; }
    else if (strview_equal(&op, "+")) { ast_node->op = OPERATOR_POS; }

    TSNode operand_node = ts_node_child_by_field_id(bi_node, FIELD_OPERAND);
    ast_node->operand = build_expr(tp, operand_node);
    // ast_node->type = alloc_type(tp->ast_pool, type_id, sizeof(Type));
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
    ast_node->op_str = op;
    if (strview_equal(&op, "and")) { ast_node->op = OPERATOR_AND; }
    else if (strview_equal(&op, "or")) { ast_node->op = OPERATOR_OR; }
    else if (strview_equal(&op, "+")) { ast_node->op = OPERATOR_ADD; }
    else if (strview_equal(&op, "-")) { ast_node->op = OPERATOR_SUB; }
    else if (strview_equal(&op, "*")) { ast_node->op = OPERATOR_MUL; }
    else if (strview_equal(&op, "^")) { ast_node->op = OPERATOR_POW; }
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
    else if (strview_equal(&op, "|")) { ast_node->op = OPERATOR_UNION; }
    else if (strview_equal(&op, "&")) { ast_node->op = OPERATOR_INTERSECT; }
    else if (strview_equal(&op, "!")) { ast_node->op = OPERATOR_EXCLUDE; }
    else if (strview_equal(&op, "is")) { ast_node->op = OPERATOR_IS; }
    else if (strview_equal(&op, "in")) { ast_node->op = OPERATOR_IN; }
    else { printf("unknown operator: %.*s\n", (int)op.length, op.str); }

    TSNode right_node = ts_node_child_by_field_id(bi_node, FIELD_RIGHT);
    ast_node->right = build_expr(tp, right_node);

    printf("get binary type\n");
    TypeId left_type = ast_node->left->type->type_id, right_type = ast_node->right->type->type_id;
    printf("left type: %d, right type: %d\n", left_type, right_type);
    TypeId type_id;
    if (ast_node->op == OPERATOR_DIV || ast_node->op == OPERATOR_POW) {
        type_id = LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_NUMBER &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_NUMBER ? LMD_TYPE_FLOAT:LMD_TYPE_ANY;
    } 
    else if (ast_node->op == OPERATOR_ADD) {
        if (left_type == right_type && (left_type == LMD_TYPE_STRING || left_type == LMD_TYPE_BINARY ||
            left_type == LMD_TYPE_ARRAY || left_type == LMD_TYPE_LIST || left_type == LMD_TYPE_MAP)) {
            type_id = left_type;
        } 
        else if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_NUMBER &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_NUMBER) {
            type_id = max(left_type, right_type);
        }
        else {
            type_id = LMD_TYPE_ANY;
        }
    } 
    else if (ast_node->op == OPERATOR_SUB || ast_node->op == OPERATOR_MUL || ast_node->op == OPERATOR_MOD) {
        type_id = max(left_type, right_type);
    } 
    else if (ast_node->op == OPERATOR_AND || ast_node->op == OPERATOR_OR || 
        ast_node->op == OPERATOR_EQ || ast_node->op == OPERATOR_NE || 
        ast_node->op == OPERATOR_LT || ast_node->op == OPERATOR_LE || 
        ast_node->op == OPERATOR_GT || ast_node->op == OPERATOR_GE || 
        ast_node->op == OPERATOR_IS || ast_node->op == OPERATOR_IN) {
        type_id = LMD_TYPE_BOOL;
    } 
    else if (ast_node->op == OPERATOR_IDIV) {
        type_id = LMD_TYPE_INT;
    }
    else if (ast_node->op == OPERATOR_TO) {
        type_id = LMD_TYPE_RANGE;
    }
    else {
        type_id = LMD_TYPE_ANY;
    }
    ast_node->type = alloc_type(tp->ast_pool, type_id, sizeof(Type));
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
    ast_node->type = alloc_type(tp->ast_pool, type_id, sizeof(Type));
    printf("end build if expr\n");
    return (AstNode*)ast_node;
}

AstNode* build_list(Transpiler* tp, TSNode list_node) {
    printf("build list\n");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_LIST, list_node, sizeof(AstListNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_LIST, sizeof(TypeList));
    TypeList *type = (TypeList*)ast_node->type;

    ast_node->vars = (NameScope*)pool_calloc(tp->ast_pool, sizeof(NameScope));
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
    if (!ast_node->declare && type->length == 1) { return ast_node->item;}
    tp->current_scope = ast_node->vars->parent;
    return (AstNode*)ast_node;
}

AstNode* build_assign_expr(Transpiler* tp, TSNode asn_node) {
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
        AstNode *type_expr = build_expr(tp, type_node);
        ast_node->type = ((TypeType*)type_expr->type)->type;
    }

    // push the name to the name stack
    push_name(tp, ast_node, NULL);
    return (AstNode*)ast_node;
}

AstNode* build_let_stam(Transpiler* tp, TSNode let_node, TSSymbol symbol) {
    AstLetNode* ast_node = (AstLetNode*)alloc_ast_node(tp, 
        symbol == SYM_PUB_STAM ? AST_NODE_PUB_STAM : AST_NODE_LET_STAM, let_node, sizeof(AstLetNode));

    // 'let' can have multiple name-value declarations
    TSTreeCursor cursor = ts_tree_cursor_new(let_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode *prev_declare = NULL;
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            assert(ts_node_symbol(child) == SYM_ASSIGN_EXPR);
            AstNode *declare = build_assign_expr(tp, child);
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
    ast_node->type = &LIT_NULL;  // let stam returns null
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

AstNode* build_base_type(Transpiler* tp, TSNode type_node) {
    AstTypeNode* ast_node = (AstTypeNode*)alloc_ast_node(tp, AST_NODE_TYPE, type_node, sizeof(AstTypeNode));
    printf("build type annotation\n");
    StrView type_name = ts_node_source(tp, type_node);
    if (strview_equal(&type_name, "null")) {
        ast_node->type = (Type*)&LIT_TYPE_NULL;
    }
    else if (strview_equal(&type_name, "any")) {
        ast_node->type = (Type*)&LIT_TYPE_ANY;
    }
    else if (strview_equal(&type_name, "bool")) {
        ast_node->type = (Type*)&LIT_TYPE_BOOL;
    } 
    else if (strview_equal(&type_name, "int")) {
        ast_node->type = (Type*)&LIT_TYPE_INT;
    }
    else if (strview_equal(&type_name, "float")) {
        ast_node->type = (Type*)&LIT_TYPE_FLOAT;
    }
    else if (strview_equal(&type_name, "decimal")) {
        ast_node->type = (Type*)&LIT_TYPE_DECIMAL;
    }    
    else if (strview_equal(&type_name, "number")) {
        ast_node->type = (Type*)&LIT_TYPE_NUMBER;
    }
    else if (strview_equal(&type_name, "string")) {
        ast_node->type = (Type*)&LIT_TYPE_STRING;
    }
    else if (strview_equal(&type_name, "symbol")) {
        ast_node->type = (Type*)&LIT_TYPE_SYMBOL;
    }
    else if (strview_equal(&type_name, "datetime")) {
        ast_node->type = (Type*)&LIT_TYPE_DTIME;
    }
    else if (strview_equal(&type_name, "time")) {
        ast_node->type = (Type*)&LIT_TYPE_DTIME;
    }
    else if (strview_equal(&type_name, "date")) {
        ast_node->type = (Type*)&LIT_TYPE_DTIME;
    }
    else if (strview_equal(&type_name, "binary")) {
        ast_node->type = (Type*)&LIT_TYPE_BINARY;
    }   
    else if (strview_equal(&type_name, "list")) {
        ast_node->type = (Type*)&LIT_TYPE_LIST;
    }
    else if (strview_equal(&type_name, "array")) {
        ast_node->type = (Type*)&LIT_TYPE_ARRAY;
    }
    else if (strview_equal(&type_name, "map")) {
        ast_node->type = (Type*)&LIT_TYPE_MAP;
    }
    else if (strview_equal(&type_name, "element")) {
        ast_node->type = (Type*)&LIT_TYPE_ELMT;
    }    
    // else if (strview_equal(&type_name, "object")) {
    //     ast_node->type = (Type*)&LIT_TYPE_OBJECT;
    // }    
    else if (strview_equal(&type_name, "function")) {
        ast_node->type = (Type*)&LIT_TYPE_FUNC;
    }
    else if (strview_equal(&type_name, "type")) {
        ast_node->type = (Type*)&LIT_TYPE_TYPE;
    }
    else if (strview_equal(&type_name, "error")) {
        ast_node->type = (Type*)&LIT_TYPE_ERROR;
    }
    else {
        printf("unknown base type %.*s\n", (int)type_name.length, type_name.str);
        ast_node->type = (Type*)&LIT_TYPE_ERROR;
    }
    printf("built base type %.*s, type_id %d\n", (int)type_name.length, type_name.str, 
        ((TypeType*)ast_node->type)->type->type_id);
    return (AstNode*)ast_node;   
}

AstNode* build_list_type(Transpiler* tp, TSNode list_node) {
    printf("build list type\n");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_LIST_TYPE, list_node, sizeof(AstListNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeList *type = (TypeList*)alloc_type(tp->ast_pool, LMD_TYPE_LIST, sizeof(TypeList));
    ((TypeType*)ast_node->type)->type = (Type*)type;

    TSNode child = ts_node_named_child(list_node, 0);
    AstNode *prev_declare = NULL, *prev_item = NULL;
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
        child = ts_node_next_named_sibling(child);
    }

    arraylist_append(tp->type_list, ast_node->type);
    type->type_index = tp->type_list->length - 1;
    // todo: if (!ast_node->declare && type->length == 1) { return ast_node->item; }
    return (AstNode*)ast_node;
}

AstNode* build_array_type(Transpiler* tp, TSNode array_node) {
    printf("build array type\n");
    AstArrayNode* ast_node = (AstArrayNode*)alloc_ast_node(tp, AST_NODE_ARRAY_TYPE, array_node, sizeof(AstArrayNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeArray *type = (TypeArray*)alloc_type(tp->ast_pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
    ((TypeType*)ast_node->type)->type = (Type*)type;

    TSNode child = ts_node_named_child(array_node, 0);
    AstNode* prev_item = NULL;  Type *nested_type = NULL;
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

    arraylist_append(tp->type_list, ast_node->type);
    type->type_index = tp->type_list->length - 1;
    return (AstNode*)ast_node;
}

AstNode* build_map_type(Transpiler* tp, TSNode map_node) {
    AstMapNode* ast_node = (AstMapNode*)alloc_ast_node(tp, AST_NODE_MAP_TYPE, map_node, sizeof(AstMapNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeMap *type = (TypeMap*)alloc_type(tp->ast_pool, LMD_TYPE_MAP, sizeof(TypeMap));
    ((TypeType*)ast_node->type)->type = (Type*)type;

    TSNode child = ts_node_named_child(map_node, 0);
    AstNode* prev_item = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    // map type does not support dynamic expr in the body
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        AstNode* item = (AstNode*)build_key_expr(tp, child);
        if (!prev_item) { ast_node->item = item; } 
        else { prev_item->next = item; }
        prev_item = item;

        ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->ast_pool, sizeof(ShapeEntry));
        shape_entry->name = &((AstNamedNode*)item)->name;
        shape_entry->type = item->type;
        shape_entry->byte_offset = byte_offset;
        if (!prev_entry) { type->shape = shape_entry; } 
        else { prev_entry->next = shape_entry; }
        prev_entry = shape_entry;

        type->length++;  byte_offset += sizeof(void*);
        child = ts_node_next_named_sibling(child);
    }
    type->byte_size = byte_offset;

    arraylist_append(tp->type_list, ast_node->type);
    type->type_index = tp->type_list->length - 1;
    return (AstNode*)ast_node;
}

AstNode* build_content_type(Transpiler* tp, TSNode list_node) {
    printf("build content type\n");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_CONTENT_TYPE, list_node, sizeof(AstListNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_LIST, sizeof(TypeList));
    TypeList *type = (TypeList*)ast_node->type;
    TSNode child = ts_node_named_child(list_node, 0);
    AstNode* prev_item = NULL;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        AstNode* item = build_expr(tp, child);
        if (item) {
            if (!prev_item) ast_node->item = item;
            else prev_item->next = item;
            prev_item = item;
            type->length++;
        }
        // else comment or error
        child = ts_node_next_named_sibling(child);
    }
    printf("end building content type: %ld\n", type->length);
    return (AstNode*)ast_node;
}

AstNode* build_element_type(Transpiler* tp, TSNode elmt_node) {
    printf("build element type\n");
    AstElementNode* ast_node = (AstElementNode*)alloc_ast_node(tp, 
        AST_NODE_ELMT_TYPE, elmt_node, sizeof(AstElementNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeElmt *type  = (TypeElmt*)alloc_type(tp->ast_pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    ((TypeType*)ast_node->type)->type = (Type*)type;

    TSNode child = ts_node_named_child(elmt_node, 0);
    AstNode* prev_item = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        if (symbol == SYM_IDENT) {  // element name
            StrView name = ts_node_source(tp, child);
            type->name = name;
        }
        else if (symbol == SYM_CONTENT_TYPE) {  // element content
            ast_node->content = build_content_type(tp, child);
        }        
        else {  // attrs
            AstNode* item = (AstNode*)build_key_expr(tp, child);
            if (!prev_item) { ast_node->item = item; } 
            else { prev_item->next = item; }
            prev_item = item;

            ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->ast_pool, sizeof(ShapeEntry));
            shape_entry->name = &((AstNamedNode*)item)->name;
            shape_entry->type = item->type;           
            shape_entry->byte_offset = byte_offset;
            if (!prev_entry) { type->shape = shape_entry; } 
            else { prev_entry->next = shape_entry; }
            prev_entry = shape_entry;

            type->length++;  byte_offset += sizeof(void*);
        }
        child = ts_node_next_named_sibling(child);
    }

    arraylist_append(tp->type_list, ast_node->type);
    type->type_index = tp->type_list->length - 1;
    type->byte_size = byte_offset;
    type->content_length = ast_node->content ? ((TypeList*)ast_node->content->type)->length : 0;
    return (AstNode*)ast_node;
}

AstNode* build_func_type(Transpiler* tp, TSNode func_node) {
    printf("build fn type\n");
    AstFuncNode* ast_node = (AstFuncNode*)alloc_ast_node(tp, AST_NODE_FUNC_TYPE, func_node, sizeof(AstFuncNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeFunc *fn_type = (TypeFunc*) alloc_type(tp->ast_pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
    ((TypeType*)ast_node->type)->type = (Type*)fn_type;

    // build the params
    ast_node->vars = (NameScope*)pool_calloc(tp->ast_pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    tp->current_scope = ast_node->vars;
    TSTreeCursor cursor = ts_tree_cursor_new(func_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNamedNode *prev_param = NULL;  int param_count = 0;
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {  // param declaration
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNamedNode *param = build_param_expr(tp, child, true);
            printf("got param type %d\n", param->node_type);
            if (prev_param == NULL) {
                ast_node->param = param;
                fn_type->param = (TypeParam*)param->type;
            } else {
                prev_param->next = (AstNode*)param;
                ((TypeParam*)prev_param->type)->next = (TypeParam*)param->type;
            }
            prev_param = param;  param_count++;
        }
        else if (field_id == FIELD_TYPE) {  // return type
            TSNode child = ts_tree_cursor_current_node(&cursor);            
            AstNode *type_expr = build_expr(tp, child);
            fn_type->returned = ((TypeType*)type_expr->type)->type;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    fn_type->param_count = param_count;

    arraylist_append(tp->type_list, ast_node->type);
    fn_type->type_index = tp->type_list->length - 1;
    printf("func type index: %d\n", fn_type->type_index);
    return (AstNode*)ast_node;
}

AstNode* build_primary_type(Transpiler* tp, TSNode type_node) {
    printf("build primary type\n");
    TSNode child = ts_node_named_child(type_node, 0);
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        switch (symbol) {
        case SYM_BASE_TYPE:
            return build_base_type(tp, child);
        case SYM_ARRAY_TYPE:
            return build_array_type(tp, child);
        case SYM_LIST_TYPE:
            return build_list_type(tp, child);
        case SYM_MAP_TYPE:
            return build_map_type(tp, child);
        case SYM_ELEMENT_TYPE:
            return build_element_type(tp, child);
        case SYM_FN_TYPE:
            return build_func_type(tp, child);
        default: // literal values
            return build_expr(tp, child);
        }
        child = ts_node_next_named_sibling(child);
    }
    return NULL;
}

AstNode* build_binary_type(Transpiler* tp, TSNode bi_node) {
    printf("build binary type\n");
    AstBinaryNode* ast_node = (AstBinaryNode*)alloc_ast_node(tp, 
        AST_NODE_BINARY_TYPE, bi_node, sizeof(AstBinaryNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeBinary *type  = (TypeBinary*)alloc_type(tp->ast_pool, LMD_TYPE_BINARY, sizeof(TypeBinary));
    ((TypeType*)ast_node->type)->type = (Type*)type;

    TSNode left_node = ts_node_child_by_field_id(bi_node, FIELD_LEFT);
    ast_node->left = build_expr(tp, left_node);

    TSNode op_node = ts_node_child_by_field_id(bi_node, FIELD_OPERATOR);
    StrView op = ts_node_source(tp, op_node);
    ast_node->op_str = op;
    if (strview_equal(&op, "|")) { ast_node->op = OPERATOR_UNION; }
    else if (strview_equal(&op, "&")) { ast_node->op = OPERATOR_OR; }
    else if (strview_equal(&op, "!")) { ast_node->op = OPERATOR_EXCLUDE; }
    else { printf("unknown operator: %.*s\n", (int)op.length, op.str); }

    TSNode right_node = ts_node_child_by_field_id(bi_node, FIELD_RIGHT);
    ast_node->right = build_expr(tp, right_node);

    type->left = ast_node->left->type;
    type->right = ast_node->right->type;
    type->op = ast_node->op;
    arraylist_append(tp->type_list, ast_node->type);
    type->type_index = tp->type_list->length - 1;
    printf("binary type index: %d\n", type->type_index);
    return (AstNode*)ast_node;
}

AstNode* build_map(Transpiler* tp, TSNode map_node) {
    AstMapNode* ast_node = (AstMapNode*)alloc_ast_node(tp, AST_NODE_MAP, map_node, sizeof(AstMapNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_MAP, sizeof(TypeMap));
    TypeMap *type = (TypeMap*)ast_node->type;

    TSNode child = ts_node_named_child(map_node, 0);
    AstNode* prev_item = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        // named map item, or dynamic expr
        AstNode* item = (symbol == SYM_MAP_ITEM) ? (AstNode*)build_key_expr(tp, child) : build_expr(tp, child);
        if (!prev_item) { ast_node->item = item; } 
        else { prev_item->next = item; }
        prev_item = item;

        ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->ast_pool, sizeof(ShapeEntry));
        shape_entry->name = (symbol == SYM_MAP_ITEM) ? &((AstNamedNode*)item)->name : NULL;
        shape_entry->type = item->type;
        if (!shape_entry->name && !(item->type->type_id == LMD_TYPE_MAP || item->type->type_id == LMD_TYPE_ANY)) {
            printf("invalid map item type %d, should be map or any\n", item->type->type_id);
        }
        shape_entry->byte_offset = byte_offset;
        if (!prev_entry) { type->shape = shape_entry; } 
        else { prev_entry->next = shape_entry; }
        prev_entry = shape_entry;

        type->length++;
        byte_offset += (symbol == SYM_MAP_ITEM) ? type_info[item->type->type_id].byte_size : sizeof(void*);
        child = ts_node_next_named_sibling(child);
    }
    type->byte_size = byte_offset;

    arraylist_append(tp->type_list, type);
    type->type_index = tp->type_list->length - 1;
    return (AstNode*)ast_node;
}

AstNode* build_elmt(Transpiler* tp, TSNode elmt_node) {
    printf("build element expr\n");
    AstElementNode* ast_node = (AstElementNode*)alloc_ast_node(tp, 
        AST_NODE_ELEMENT, elmt_node, sizeof(AstElementNode));
    TypeElmt *type  = (TypeElmt*)alloc_type(tp->ast_pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    ast_node->type = (Type*)type;

    TSNode child = ts_node_named_child(elmt_node, 0);
    AstNode* prev_item = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        if (symbol == SYM_IDENT) {  // element name
            StrView name = ts_node_source(tp, child);
            type->name = name;
        }
        else if (symbol == SYM_CONTENT) {  // element content
            ast_node->content = build_content(tp, child, false, false);
        }        
        else {  // attrs
            AstNode* item = (symbol == SYM_ATTR) ? (AstNode*)build_key_expr(tp, child) : build_expr(tp, child);
            if (!prev_item) { ast_node->item = item; } 
            else { prev_item->next = item; }
            prev_item = item;

            ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->ast_pool, sizeof(ShapeEntry));
            shape_entry->name = (symbol == SYM_ATTR) ? &((AstNamedNode*)item)->name : NULL;
            shape_entry->type = item->type;
            if (!shape_entry->name && !(item->type->type_id == LMD_TYPE_MAP || item->type->type_id == LMD_TYPE_ANY)) {
                printf("invalid map item type %d, should be map or any\n", item->type->type_id);
            }            
            shape_entry->byte_offset = byte_offset;
            if (!prev_entry) { type->shape = shape_entry; } 
            else { prev_entry->next = shape_entry; }
            prev_entry = shape_entry;

            type->length++;
            byte_offset += (symbol == SYM_ATTR) ? type_info[item->type->type_id].byte_size : sizeof(void*);
        }
        child = ts_node_next_named_sibling(child);
    }

    arraylist_append(tp->type_list, type);
    type->type_index = tp->type_list->length - 1;
    type->byte_size = byte_offset;
    type->content_length = ast_node->content ? ((TypeList*)ast_node->content->type)->length : 0;
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
    Type *expr_type = ast_node->as->type;
    ast_node->type = 
        expr_type->type_id == LMD_TYPE_ARRAY || expr_type->type_id == LMD_TYPE_LIST ? 
            ((TypeArray*)expr_type)->nested : 
        expr_type->type_id == LMD_TYPE_RANGE ? &TYPE_INT : expr_type;

    // push the name to the name stack
    push_name(tp, ast_node, NULL);
    return (AstNode*)ast_node;
}

AstNode* build_for_expr(Transpiler* tp, TSNode for_node) {
    printf("build for expr\n");
    AstForNode* ast_node = (AstForNode*)alloc_ast_node(tp, AST_NODE_FOR_EXPR, for_node, sizeof(AstForNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_ANY, sizeof(Type));

    ast_node->vars = (NameScope*)pool_calloc(tp->ast_pool, sizeof(NameScope));
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

    tp->current_scope = ast_node->vars->parent;
    return (AstNode*)ast_node;
}

AstNamedNode* build_param_expr(Transpiler* tp, TSNode param_node, bool is_type) {
    printf("build param expr\n");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_PARAM, param_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(param_node, FIELD_NAME);
    StrView name_str = ts_node_source(tp, name);
    ast_node->name = name_str;

    TSNode type_node = ts_node_child_by_field_id(param_node, FIELD_TYPE);
    // determine the type of the field
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_ANY, sizeof(TypeParam));
    if (!ts_node_is_null(type_node)) {
        AstNode *type_expr = build_expr(tp, type_node);
        *ast_node->type = *((TypeType*)type_expr->type)->type;
    } else {
        *ast_node->type = ast_node->as ? *ast_node->as->type : TYPE_ANY;
    }

    if (!is_type) { push_name(tp, ast_node, NULL); }
    return ast_node;
}

AstNode* build_func(Transpiler* tp, TSNode func_node, bool is_named, bool is_global) {
    printf("build function\n");
    AstFuncNode* ast_node = (AstFuncNode*)alloc_ast_node(tp,
        is_named ? AST_NODE_FUNC : AST_NODE_FUNC_EXPR, func_node, sizeof(AstFuncNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
    TypeFunc *fn_type = (TypeFunc*) ast_node->type;
    fn_type->is_anonymous = !is_named;  
    
    // 'pub' flag
    TSNode pub = ts_node_child_by_field_id(func_node, FIELD_PUB);
    fn_type->is_public = !ts_node_is_null(pub);

    // get the function name
    if (is_named) {
        TSNode fn_name_node = ts_node_child_by_field_id(func_node, FIELD_NAME);
        StrView name = ts_node_source(tp, fn_name_node);
        ast_node->name = name;
        // add fn name to current scope
        push_name(tp, (AstNamedNode*)ast_node, NULL);
    }

    // build the params
    ast_node->vars = (NameScope*)pool_calloc(tp->ast_pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    tp->current_scope = ast_node->vars;
    TSTreeCursor cursor = ts_tree_cursor_new(func_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNamedNode *prev_param = NULL;  int param_count = 0;
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {  // param declaration
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNamedNode *param = build_param_expr(tp, child, false);
            printf("got param type %d\n", param->node_type);
            if (prev_param == NULL) {
                ast_node->param = param;
                fn_type->param = (TypeParam*)param->type;
            } else {
                prev_param->next = (AstNode*)param;
                ((TypeParam*)prev_param->type)->next = (TypeParam*)param->type;
            }
            prev_param = param;  param_count++;
        }
        else if (field_id == FIELD_TYPE) {  // return type
            TSNode child = ts_tree_cursor_current_node(&cursor);            
            AstNode *type_expr = build_expr(tp, child);
            fn_type->returned = ((TypeType*)type_expr->type)->type;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    fn_type->param_count = param_count;

    // build the function body
    // ast_node->locals = (NameScope*)pool_calloc(tp->ast_pool, sizeof(NameScope));
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

AstNode* build_content(Transpiler* tp, TSNode list_node, bool flattern, bool is_global) {
    printf("build content\n");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_CONTENT, list_node, sizeof(AstListNode));
    ast_node->type = alloc_type(tp->ast_pool, LMD_TYPE_LIST, sizeof(TypeList));
    TypeList *type = (TypeList*)ast_node->type;
    TSNode child = ts_node_named_child(list_node, 0);
    AstNode* prev_item = NULL;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        AstNode* item = symbol == SYM_FUNC_STAM || symbol == SYM_FUNC_EXPR_STAM ? 
            build_func(tp, child, true, is_global) : build_expr(tp, child);
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
    printf("end building content item: %p, %ld\n", ast_node->item, type->length);
    if (flattern && type->length == 1) { return ast_node->item;}
    return (AstNode*)ast_node;
}

AstNode* build_lit_node(Transpiler* tp, TSNode lit_node, bool quoted_value, TSSymbol symbol) {
    printf("build lit node\n");
    AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, lit_node, sizeof(AstPrimaryNode));
    TSNode val_node = quoted_value ? ts_node_named_child(lit_node, 0) : lit_node;
    ast_node->type = build_lit_string(tp, val_node, symbol);
    return (AstNode*)ast_node;
}

AstNode* build_expr(Transpiler* tp, TSNode expr_node) {
    // get the function name
    TSSymbol symbol = ts_node_symbol(expr_node);
    switch (symbol) {
    case SYM_PRIMARY_EXPR:
        return build_primary_expr(tp, expr_node);
    case SYM_UNARY_EXPR:
        return build_unary_expr(tp, expr_node);
    case SYM_BINARY_EXPR:
        return build_binary_expr(tp, expr_node);
    case SYM_LET_STAM:  case SYM_PUB_STAM:
        // todo: full type def support 
        return build_let_stam(tp, expr_node, symbol);
    case SYM_FOR_EXPR:
        return build_for_expr(tp, expr_node);
    case SYM_FOR_STAM:
        return build_for_expr(tp, expr_node);
    case SYM_IF_EXPR:
        return build_if_expr(tp, expr_node);
    case SYM_IF_STAM:
        return build_if_expr(tp, expr_node);
    case SYM_ASSIGN_EXPR:
        return build_assign_expr(tp, expr_node);
    case SYM_ARRAY:
        return build_array(tp, expr_node);
    case SYM_MAP:
        return build_map(tp, expr_node);
    case SYM_ELEMENT:
        return build_elmt(tp, expr_node);
    case SYM_CONTENT:
        return build_content(tp, expr_node, true, false);
    case SYM_LIST:
        return build_list(tp, expr_node);
    case SYM_IDENT:
        return build_identifier(tp, expr_node);
    case SYM_FUNC_STAM:
        return build_func(tp, expr_node, true, false);
    case SYM_FUNC_EXPR_STAM:
        return build_func(tp, expr_node, true, false);
    case SYM_FUNC_EXPR:  // anonymous function
        return build_func(tp, expr_node, false, false);
    case SYM_STRING:  case SYM_SYMBOL:  case SYM_BINARY:
        return build_lit_node(tp, expr_node, true, symbol);
    case SYM_DATETIME:  case SYM_TIME:
        return build_lit_node(tp, expr_node, false, symbol);
        break;
    case SYM_TRUE:  case SYM_FALSE:
        AstPrimaryNode* b_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        b_node->type = &LIT_BOOL;
        return (AstNode*)b_node;
    case SYM_INT:
        AstPrimaryNode* i_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        i_node->type = &LIT_INT;   // todo: check int value range
        return (AstNode*)i_node;
    case SYM_FLOAT:
        AstPrimaryNode* f_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        f_node->type = build_lit_float(tp, expr_node, symbol);
        return (AstNode*)f_node;
    case SYM_BASE_TYPE:
        return build_base_type(tp, expr_node);
    case SYM_PRIMARY_TYPE:
        return build_primary_type(tp, expr_node);
    case SYM_BINARY_TYPE:
        return build_binary_type(tp, expr_node);
    case SYM_TYPE_DEFINE:
        // todo: full type def support 
        return build_let_stam(tp, expr_node, symbol);
    case SYM_IMPORT_MODULE:
        // already processed
        return NULL;
    case SYM_COMMENT:
        return NULL;
    default:
        printf("unknown syntax node: %s\n", ts_node_type(expr_node));
        return NULL;
    }
}

void declare_module_import(Transpiler* tp, AstImportNode *import_node) {
    printf("declare import module\n");
    // import module
    if (!import_node->script) { printf("misssing script\n");  return; }
    printf("script reference: %s\n", import_node->script->reference);
    // loop through the public functions in the module
    AstNode *node = import_node->script->ast_root;
    if (!node) { printf("misssing root node\n");  return; }
    assert(node->node_type == AST_SCRIPT);
    node = ((AstScript*)node)->child;
    printf("finding content node\n");
    while (node) {
        if (node->node_type == AST_NODE_CONTENT) break;
        node = node->next;
    }
    if (!node) { printf("misssing content node\n");  return; }
    node = ((AstListNode*)node)->item;
    while (node) {
        if (node->node_type == AST_NODE_FUNC) {
            AstFuncNode *func_node = (AstFuncNode*)node;
            printf("got fn: %.*s, is_public: %d\n", (int)func_node->name.length, func_node->name.str, 
                ((TypeFunc*)func_node->type)->is_public);
            if (((TypeFunc*)func_node->type)->is_public) {
                push_name(tp, (AstNamedNode*)func_node, import_node);
            }
        }
        else if (node->node_type == AST_NODE_PUB_STAM) {
            AstLetNode *pub_node = (AstLetNode*)node;
            AstNode *declare = pub_node->declare;
            while (declare) {
                AstNamedNode *dec_node = (AstNamedNode*)declare;
                push_name(tp, (AstNamedNode*)dec_node, import_node);
                printf("got pub var: %.*s\n", (int)dec_node->name.length, dec_node->name.str);
                declare = declare->next;
            }
        }
        node = node->next;
    }
}

AstNode* build_module_import(Transpiler* tp, TSNode import_node) {
    printf("build import module\n");
    AstImportNode* ast_node = (AstImportNode*)alloc_ast_node(
        tp, AST_NODE_IMPORT, import_node, sizeof(AstImportNode));
        ast_node->type = &TYPE_NULL;
    TSTreeCursor cursor = ts_tree_cursor_new(import_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        TSNode child = ts_tree_cursor_current_node(&cursor);
        if (field_id == FIELD_ALIAS) {
            StrView alias = ts_node_source(tp, child);
            ast_node->alias = alias;
        }
        else if (field_id == FIELD_MODULE) {
            StrView module = ts_node_source(tp, child);
            ast_node->module = module;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    if (ast_node->module.length) {
        if (ast_node->module.str[0] == '.') {
            // convert relative import to path
            printf("runtime: %p\n", tp->runtime);
            printf("runtime dir: %s\n", tp->runtime->current_dir);
            StrBuf *buf = strbuf_new();
            strbuf_append_format(buf, "%s%.*s", tp->runtime->current_dir, 
                (int)ast_node->module.length - 1, ast_node->module.str + 1);
            char* ch = buf->str + buf->length - (ast_node->module.length - 1);
            while (*ch) { if (*ch == '.') *ch = '/';  ch++; }
            strbuf_append_str(buf, ".ls");
            ast_node->script = load_script(tp->runtime, buf->str, NULL);
            strbuf_free(buf);
            // import names/definitions from the modules
            declare_module_import(tp, ast_node);
        }
        else {
            printf("module type not supported yet: %.*s\n", (int)ast_node->module.length, ast_node->module.str);
        }
    }
    return (AstNode*)ast_node;
}

AstNode* build_script(Transpiler* tp, TSNode script_node) {
    printf("build script\n");
    AstScript* ast_node = (AstScript*)alloc_ast_node(tp, AST_SCRIPT, script_node, sizeof(AstScript));
    tp->current_scope = ast_node->global_vars = (NameScope*)pool_calloc(tp->ast_pool, sizeof(NameScope));

    // build the script body
    TSNode child = ts_node_named_child(script_node, 0);
    AstNode* prev = NULL;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        AstNode* ast = NULL;
        switch (symbol) {
        case SYM_IMPORT_MODULE:
            // import module
            ast = build_module_import(tp, child);
            break;
        case SYM_CONTENT:
            ast = build_content(tp, child, true, true);
            break;
        case SYM_COMMENT:
            // skip comments
            break;
        default:
            printf("unknown script child: %s\n", ts_node_type(child));
        }
        // AstNode* ast = symbol == SYM_CONTENT ? build_content(tp, child, true, true) : build_expr(tp, child);
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
