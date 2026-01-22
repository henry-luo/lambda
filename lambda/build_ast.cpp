#include "transpiler.hpp"
#include "../lib/hashmap.h"
#include "../lib/datetime.h"
#include "../lib/log.h"
#include <errno.h>
#include <algorithm>  // for std::max

AstNamedNode* build_param_expr(Transpiler* tp, TSNode param_node, bool is_type);
AstNode* build_occurrence_type(Transpiler* tp, TSNode occurrence_node);

// todo: properly define fn param types
SysFuncInfo sys_funcs[] = {
    {SYSFUNC_LEN, "len", 1, &TYPE_INT64, false, false},
    {SYSFUNC_TYPE, "type", 1, &TYPE_TYPE, false, false},
    {SYSFUNC_INT, "int", 1, &TYPE_ANY, false, false}, // return ANY, as there can be error
    {SYSFUNC_INT64, "int64", 1, &TYPE_INT64, false, false},
    {SYSFUNC_FLOAT, "float", 1, &TYPE_FLOAT, false, false},
    {SYSFUNC_DECIMAL, "decimal", 1, &TYPE_DECIMAL, false, false},
    {SYSFUNC_BINARY, "binary", 1, &TYPE_BINARY, false, false},
    {SYSFUNC_NUMBER, "number", 1, &TYPE_ANY, false, false},
    {SYSFUNC_STRING, "string", 1, &TYPE_STRING, false, false},
    {SYSFUNC_SYMBOL, "symbol", 1, &TYPE_SYMBOL, false, false},
    {SYSFUNC_DATETIME, "datetime", 1, &TYPE_DTIME, false, false},
    {SYSFUNC_DATE, "date", 1, &TYPE_DTIME, false, false},
    {SYSFUNC_TIME, "time", 1, &TYPE_DTIME, false, false},
    {SYSFUNC_JUSTNOW, "justnow", 0, &TYPE_DTIME, false, false},
    {SYSFUNC_SET, "set", -1, &TYPE_ANY, false, false},
    {SYSFUNC_SLICE, "slice", -1, &TYPE_ANY, false, false},
    {SYSFUNC_ALL, "all", 1, &TYPE_BOOL, false, false},
    {SYSFUNC_ANY, "any", 1, &TYPE_BOOL, false, false},
    {SYSFUNC_MIN1, "min", 1, &TYPE_ANY, false, true}, // TYPE_NUMBER;
    {SYSFUNC_MIN2, "min", 2, &TYPE_ANY, false, true}, // TYPE_NUMBER;
    {SYSFUNC_MAX1, "max", 1, &TYPE_ANY, false, true}, // TYPE_NUMBER;
    {SYSFUNC_MAX2, "max", 2, &TYPE_ANY, false, true}, // TYPE_NUMBER;
    {SYSFUNC_SUM, "sum", 1, &TYPE_ANY, false, false}, // TYPE_NUMBER;
    {SYSFUNC_AVG, "avg", 1, &TYPE_ANY, false, false}, // TYPE_NUMBER;
    {SYSFUNC_ABS, "abs", 1, &TYPE_ANY, false, false}, // TYPE_NUMBER;
    {SYSFUNC_ROUND, "round", 1, &TYPE_ANY, false, false}, // TYPE_NUMBER;
    {SYSFUNC_FLOOR, "floor", 1, &TYPE_ANY, false, false}, // TYPE_NUMBER;
    {SYSFUNC_CEIL, "ceil", 1, &TYPE_ANY, false, false}, // TYPE_NUMBER;
    {SYSFUNC_INPUT1, "input", 1, &TYPE_ANY, false, true},
    {SYSFUNC_INPUT2, "input", 2, &TYPE_ANY, false, true},
    {SYSFUNC_FORMAT1, "format", 1, &TYPE_STRING, false, true},
    {SYSFUNC_FORMAT2, "format", 2, &TYPE_STRING, false, true},
    {SYSFUNC_ERROR, "error", 1, &TYPE_ERROR, false, false},
    {SYSFUNC_NORMALIZE, "normalize", 1, &TYPE_STRING, false},
    // {SYSFUNC_SUBSTRING, "substring", 2, &TYPE_ANY},
    // {SYSFUNC_CONTAINS, "contains", 2, &TYPE_ANY},
    {SYSPROC_NOW, "now", 0, &TYPE_DTIME, true, false},
    {SYSPROC_TODAY, "today", 0, &TYPE_DTIME, true, false},
    {SYSPROC_PRINT, "print", 1, &TYPE_NULL, true, false},
    {SYSPROC_FETCH, "fetch", 2, &TYPE_ANY, true, false},
    {SYSPROC_OUTPUT, "output", 2, &TYPE_ANY, true, false},
    {SYSPROC_CMD, "cmd", 2, &TYPE_ANY, true, false},
};

SysFuncInfo* get_sys_func_info(StrView* name, int arg_count) {
    for (size_t i = 0; i < sizeof(sys_funcs) / sizeof(sys_funcs[0]); i++) {
        if (strview_equal(name, sys_funcs[i].name) && sys_funcs[i].arg_count == arg_count) {
            log_debug("is sys func: %.*s, %d", (int)name->length, name->str, arg_count);
            return &sys_funcs[i];
        }
    }
    log_debug("don't have sys func: %.*s, %d", (int)name->length, name->str, arg_count);
    return NULL;
}

// Check if arg_type is compatible with param_type for function calls
bool types_compatible(Type* arg_type, Type* param_type) {
    if (!arg_type || !param_type) return true;  // unknown types are compatible
    if (param_type->type_id == LMD_TYPE_ANY) return true;  // any accepts all
    if (arg_type->type_id == LMD_TYPE_ANY) return true;  // any arg can pass to typed param (runtime check)
    if (arg_type->type_id == param_type->type_id) return true;
    
    // Numeric coercion: int → int64, int → float, int64 → float
    if (param_type->type_id == LMD_TYPE_FLOAT) {
        if (arg_type->type_id == LMD_TYPE_INT || 
            arg_type->type_id == LMD_TYPE_INT64) return true;
    }
    if (param_type->type_id == LMD_TYPE_INT64) {
        if (arg_type->type_id == LMD_TYPE_INT) return true;
    }
    if (param_type->type_id == LMD_TYPE_NUMBER) {
        if (arg_type->type_id == LMD_TYPE_INT || 
            arg_type->type_id == LMD_TYPE_INT64 ||
            arg_type->type_id == LMD_TYPE_FLOAT ||
            arg_type->type_id == LMD_TYPE_DECIMAL) return true;
    }
    
    return false;
}

// Record a type error and check if we should continue transpiling
void record_type_error(Transpiler* tp, int line, const char* format, ...) {
    tp->error_count++;
    
    // Format and log error message
    char error_msg[512];
    va_list args;
    va_start(args, format);
    vsnprintf(error_msg, sizeof(error_msg), format, args);
    va_end(args);
    
    log_error("type_error (line %d): %s", line, error_msg);
    
    // Check threshold
    if (tp->error_count >= tp->max_errors) {
        log_error("error_threshold: max errors (%d) reached", tp->max_errors);
    }
}

// Check if should continue transpiling based on error count
bool should_continue_transpiling(Transpiler* tp) {
    return tp->error_count < tp->max_errors;
}

mpd_t* str_to_decimal(char* str, mpd_context_t* ctx) {
    mpd_t* dec_val = mpd_new(ctx);
    if (dec_val == NULL) {
        log_error("ERROR: Failed to allocate mpdec");
        return NULL;
    }
    // parse the decimal string
    uint32_t status = 0;
    mpd_qset_string(dec_val, str, ctx, &status);
    if (status != 0) {
        log_error("ERROR: Failed to parse decimal string: %s (status: %u)", str, status);
        mpd_free(dec_val);
        return NULL;
    }
    return dec_val;
}

AstNode* alloc_ast_node(Transpiler* tp, AstNodeType node_type, TSNode node, size_t size) {
    AstNode* ast_node = (AstNode*)pool_alloc(tp->pool, size);
    memset(ast_node, 0, size);
    ast_node->node_type = node_type;  ast_node->node = node;
    return ast_node;
}

void* alloc_const(Transpiler* tp, size_t size) {
    void* bytes = pool_alloc(tp->pool, size);
    memset(bytes, 0, size);
    return bytes;
}

void push_name(Transpiler* tp, AstNamedNode* node, AstImportNode* import) {
    log_debug("pushing name %.*s, %p", (int)node->name->len, node->name->chars, node->type);
    NameEntry* entry = (NameEntry*)pool_calloc(tp->pool, sizeof(NameEntry));
    entry->name = node->name;
    entry->node = (AstNode*)node;  entry->import = import;
    if (!tp->current_scope->first) { tp->current_scope->first = entry; }
    if (tp->current_scope->last) { tp->current_scope->last->next = entry; }
    tp->current_scope->last = entry;
}

AstNode* build_array(Transpiler* tp, TSNode array_node) {
    log_debug("build array expr");
    AstArrayNode* ast_node = (AstArrayNode*)alloc_ast_node(tp, AST_NODE_ARRAY, array_node, sizeof(AstArrayNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
    TypeArray* type = (TypeArray*)ast_node->type;
    TSNode child = ts_node_named_child(array_node, 0);
    AstNode* prev_item = NULL;  Type* nested_type = NULL;
    while (!ts_node_is_null(child)) {
        AstNode* item = build_expr(tp, child);
        if (item) {
            if (!prev_item) {
                ast_node->item = item;  nested_type = item->type;
                log_debug("DEBUG: First array item type_id: %d", item->type ? item->type->type_id : -1);
            }
            else {
                prev_item->next = item;
                log_debug("DEBUG: Array item type_id: %d, nested_type_id: %d",
                    item->type ? item->type->type_id : -1, nested_type ? nested_type->type_id : -1);
                if (nested_type && item->type->type_id != nested_type->type_id) {
                    log_debug("DEBUG: Type mismatch, resetting nested_type to NULL");
                    nested_type = NULL;  // type mismatch, reset the nested type to NULL
                }
            }
            prev_item = item;
            type->length++;
        }
        child = ts_node_next_named_sibling(child);
    }
    type->nested = nested_type;
    log_debug("DEBUG: Final array nested_type_id: %d", nested_type ? nested_type->type_id : -1);
    return (AstNode*)ast_node;
}

// both index and member exprs
AstNode* build_field_expr(Transpiler* tp, TSNode array_node, AstNodeType node_type) {
    log_debug("build field expr");
    AstFieldNode* ast_node = (AstFieldNode*)alloc_ast_node(tp, node_type, array_node, sizeof(AstFieldNode));
    TSNode object_node = ts_node_child_by_field_id(array_node, FIELD_OBJECT);
    ast_node->object = build_expr(tp, object_node);

    TSNode field_node = ts_node_child_by_field_id(array_node, FIELD_FIELD);
    if (node_type == AST_NODE_MEMBER_EXPR && ts_node_symbol(field_node) == SYM_IDENT) {
        // handle id node directly without name lookup
        AstIdentNode* id_node = (AstIdentNode*)alloc_ast_node(tp, AST_NODE_IDENT, field_node, sizeof(AstIdentNode));
        StrView var_name = ts_node_source(tp, field_node);
        id_node->name = name_pool_create_strview(tp->name_pool, var_name);
        log_debug("member expr field name: '%.*s'", (int)id_node->name->len, id_node->name->chars);
        ast_node->field = (AstNode*)id_node;
    }
    else {
        ast_node->field = build_expr(tp, field_node);
    }

    // defensive check: if either object or field building failed, return error
    if (!ast_node->object || !ast_node->field) {
        log_error("Error: Failed to build field expression - object or field is null");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }
    // additional safety: check if object has valid type
    if (!ast_node->object->type) {
        log_error("Error: Field expression object missing type information");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    if (ast_node->object->type->type_id == LMD_TYPE_ARRAY) {
        // todo: fast path for array_get
        // Type* nested = ((TypeArray*)ast_node->object->type)->nested;
        // if (nested && nested->is_const) {  // need to copy the type to remove is_const flag
        //     Type* type = alloc_type(tp->pool, nested->type_id, sizeof(Type));
        //     type->is_const = 0;  // defensive code
        //     ast_node->type = type;
        // }
        // else { ast_node->type = nested ? nested : &TYPE_ANY; }
        ast_node->type = &TYPE_ANY;
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
    log_debug("build call expr: %d", symbol);
    AstCallNode* ast_node = (AstCallNode*)alloc_ast_node(tp,
        AST_NODE_CALL_EXPR, call_node, sizeof(AstCallNode));

    // count no. of arguments
    int arg_count = 0;
    TSTreeCursor cursor = ts_tree_cursor_new(call_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_ARGUMENT) { arg_count++; }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    log_debug("arg count: %d", arg_count);

    // build function name
    TSNode function_node = ts_node_child_by_field_id(call_node, FIELD_FUNCTION);
    StrView func_name = ts_node_source(tp, function_node);
    SysFuncInfo* sys_func_info = get_sys_func_info(&func_name, arg_count);
    if (sys_func_info) {
        log_debug("build sys call");
        if (sys_func_info->is_proc) {
            if (!tp->current_scope->is_proc) {
                log_error("Error: procedure '%.*s' cannot be called in a function",
                    (int)func_name.length, func_name.str);
                ast_node->type = &TYPE_ERROR;
                return (AstNode*)ast_node;
            }
        }
        AstSysFuncNode* fn_node = (AstSysFuncNode*)alloc_ast_node(tp,
            AST_NODE_SYS_FUNC, function_node, sizeof(AstSysFuncNode));
        fn_node->fn_info = sys_func_info;
        fn_node->type = sys_func_info->return_type;
        ast_node->function = (AstNode*)fn_node;
        ast_node->type = fn_node->type;
    }
    else {
        ast_node->function = build_expr(tp, function_node);
        if (ast_node->function->type->type_id == LMD_TYPE_FUNC) {
            TypeFunc* func_type = (TypeFunc*)ast_node->function->type;
            if (func_type->is_proc && !tp->current_scope->is_proc) {
                log_error("Error: procedure '%.*s' cannot be called in a function",
                    (int)func_name.length, func_name.str);
                ast_node->type = &TYPE_ERROR;
                return (AstNode*)ast_node;
            }
            ast_node->type = func_type->returned;
            if (!ast_node->type) { // e.g. recursive fn
                ast_node->type = &TYPE_ANY;
            }
            if (ast_node->type && ast_node->type->is_const) {
                // replicate the type to remove is_const flag - todo: fast path for const fn
                ast_node->type = alloc_type(tp->pool, ast_node->type->type_id, sizeof(Type));
            }
        }
        else {
            ast_node->type = &TYPE_ANY;
        }
    }

    // build arguments
    cursor = ts_tree_cursor_new(call_node);
    has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode* prev_argument = NULL;
    while (has_node) {
        // check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_ARGUMENT) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode* argument = build_expr(tp, child);
            log_debug("got argument: %p, &t: %p, node_type %d, type: %d", argument, argument->type, argument->node_type, argument->type->type_id);
            if (prev_argument == NULL) {
                ast_node->argument = argument;
            }
            else {
                prev_argument->next = argument;
            }
            prev_argument = argument;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    
    // Validate argument types against parameter types (for user-defined functions)
    if (ast_node->function->type->type_id == LMD_TYPE_FUNC) {
        TypeFunc* func_type = (TypeFunc*)ast_node->function->type;
        TypeParam* expected_param = func_type->param;
        AstNode* arg = ast_node->argument;
        int arg_index = 0;
        int line = ts_node_start_point(call_node).row + 1;
        
        while (arg && expected_param) {
            if (arg->type && !types_compatible(arg->type, (Type*)expected_param)) {
                record_type_error(tp, line, 
                    "argument %d has incompatible type %d, expected %d",
                    arg_index + 1, arg->type->type_id, expected_param->type_id);
                if (!should_continue_transpiling(tp)) {
                    ast_node->type = &TYPE_ERROR;
                    return (AstNode*)ast_node;
                }
            }
            arg = arg->next;
            expected_param = expected_param->next;
            arg_index++;
        }
    }
    
    log_debug("end building call expr type: %p, %d, is_const:%d", ast_node->type, ast_node->type->type_id, ast_node->type->is_const);
    return (AstNode*)ast_node;
}

NameEntry* lookup_name(Transpiler* tp, StrView var_name) {
    // lookup the name
    NameScope* scope = tp->current_scope;
    FIND_VAR_NAME:
    NameEntry* entry = scope->first;
    int entry_count = 0;
    while (entry) {
        entry_count++;
        if (entry_count > 1000) {  // Safety check for infinite loops
            log_error("ERROR: Too many entries in scope - possible infinite loop in entry list");
            return NULL;
        }

        StrView entry_name = strview_init(entry->name->chars, entry->name->len);
        log_debug("checking name: %.*s vs. %.*s",
            (int)entry_name.length, entry_name.str, (int)var_name.length, var_name.str);
        if (strview_eq(&entry_name, &var_name)) {
            break;
        }
        entry = entry->next;
    }
    if (!entry) {
        if (scope->parent) {
            // Defensive check: prevent infinite loop if parent pointer is circular
            if (scope == scope->parent) {
                log_error("Error: circular parent scope detected - breaking to prevent infinite loop");
                return NULL;
            }
            scope = scope->parent;
            log_debug("checking parent scope: %p", scope);
            goto FIND_VAR_NAME;
        }
        log_debug("missing identifier %.*s", (int)var_name.length, var_name.str);
        return NULL;
    }
    else {
        log_debug("found identifier %.*s", (int)entry->name->len, entry->name->chars);
        return entry;
    }
}

AstNode* build_identifier(Transpiler* tp, TSNode id_node) {
    log_debug("building identifier");
    AstIdentNode* ast_node = (AstIdentNode*)alloc_ast_node(tp, AST_NODE_IDENT, id_node, sizeof(AstIdentNode));

    // get the identifier name from source and create pooled string
    StrView var_name = ts_node_source(tp, id_node);
    ast_node->name = name_pool_create_strview(tp->name_pool, var_name);

    // lookup the name
    log_debug("looking up name: %.*s", (int)var_name.length, var_name.str);
    NameEntry* entry = lookup_name(tp, var_name);
    if (!entry) {
        // ident is used for member access, thus we return TYPE_ANY
        ast_node->type = &TYPE_ANY;
    }
    else {
        log_debug("found identifier %.*s", (int)entry->name->len, entry->name->chars);
        ast_node->entry = entry;
        if (entry->import && entry->node->type->type_id != LMD_TYPE_FUNC) {
            // clone and remove is_const flag
            // todo: full type clone
            log_debug("got imported identifier %.*s from module %.*s",
                (int)entry->name->len, entry->name->chars,
                (int)entry->import->module.length, entry->import->module.str);
            ast_node->type = alloc_type(tp->pool, entry->node->type->type_id, sizeof(Type));
            // defensive code
            ast_node->type->is_const = 0;
        }
        else {
            log_debug("Debug: entry->node->type is %p for identifier %.*s",
                entry->node->type, (int)entry->name->len, entry->name->chars);
            ast_node->type = entry->node->type;
            if (!ast_node->type) {
                log_warn("Warning: entry->node->type is null for identifier %.*s, using TYPE_ANY",
                    (int)entry->name->len, entry->name->chars);
                ast_node->type = &TYPE_ANY;
            }
            // Special handling: if identifier refers to a type definition, wrap type in TypeType
            else if (entry->node->node_type == AST_NODE_TYPE_STAM) {
                TypeType* type_type = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
                type_type->type = entry->node->type;
                ast_node->type = (Type*)type_type;
                log_debug("Wrapped type definition %.*s in TypeType",
                    (int)entry->name->len, entry->name->chars);
            }
        }
        if (ast_node->type) {
            log_debug("ident %p type: %d", ast_node->type, ast_node->type->type_id);
        }
        else {
            log_debug("ident %p type: null", ast_node);
        }
    }
    return (AstNode*)ast_node;
}

Type* build_lit_string(Transpiler* tp, TSNode node, TSSymbol symbol) {
    // todo: exclude zero-length string
    String* str;
    log_debug("build lit string with symbol: %d", symbol);
    TypeString* str_type = (TypeString*)alloc_type(tp->pool,
        symbol == SYM_STRING ? LMD_TYPE_STRING :
        symbol == SYM_BINARY ? LMD_TYPE_BINARY :
        LMD_TYPE_SYMBOL, sizeof(TypeString));
    str_type->is_const = 1;  str_type->is_literal = 1;

    int cnt = ts_node_named_child_count(node);
    log_debug("string child count:: %d", cnt);
    if (cnt <= 1) {
        // for SYM_IDENT, child cnt == 0
        TSNode child = cnt ? ts_node_named_child(node, 0) : node;
        int start = ts_node_start_byte(child), end = ts_node_end_byte(child);
        int len = end - start;
        // copy the string
        str = (String*)pool_alloc(tp->pool, sizeof(String) + len + 1);
        str_type->string = (String*)str;
        const char* str_content = tp->source + start;
        memcpy(str->chars, str_content, len);  // memcpy is probably faster than strcpy
        str->chars[len] = '\0';  str->len = len;
        str->ref_cnt = 1;  // set to 1 to prevent it from being freed
    }
    else { // got escape sequence
        StringBuf *str_buf = stringbuf_new(tp->pool);  // todo: reuse the stringbuf
        TSNode child = ts_node_named_child(node, 0);
        while (!ts_node_is_null(child)) {
            TSSymbol symbol = ts_node_symbol(child);
            if (symbol == SYM_STRING_CONTENT || symbol == SYM_SYMBOL_CONTENT) {
                // handle normnal string
                stringbuf_append_str_n(str_buf, tp->source + ts_node_start_byte(child), ts_node_end_byte(child) - ts_node_start_byte(child));
            }
            else if (symbol == SYM_ESCAPE_SEQUENCE) {
                // handle escape sequences: \", \\, \/, \b, \f, \n, \r, \t, \uXXXX, \u{...}
                const char* escape_start = tp->source + ts_node_start_byte(child);
                int escape_len = ts_node_end_byte(child) - ts_node_start_byte(child);

                if (escape_len >= 2 && escape_start[0] == '\\') {
                    char escape_char = escape_start[1];
                    switch (escape_char) {
                    case '"':
                        stringbuf_append_char(str_buf, '"');
                        break;
                    case '\\':
                        stringbuf_append_char(str_buf, '\\');
                        break;
                    case '/':
                        stringbuf_append_char(str_buf, '/');
                        break;
                    case 'b':
                        stringbuf_append_char(str_buf, '\b');
                        break;
                    case 'f':
                        stringbuf_append_char(str_buf, '\f');
                        break;
                    case 'n':
                        stringbuf_append_char(str_buf, '\n');
                        break;
                    case 'r':
                        stringbuf_append_char(str_buf, '\r');
                        break;
                    case 't':
                        stringbuf_append_char(str_buf, '\t');
                        break;
                    case 'u':
                        // handle Unicode escape sequences: \uXXXX or \u{...}
                        if (escape_len >= 6 && escape_start[2] != '{') {
                            // \uXXXX format (exactly 4 hex digits)
                            char hex_digits[5] = {0};
                            memcpy(hex_digits, escape_start + 2, 4);
                            char* endptr;
                            uint32_t code_point = strtoul(hex_digits, &endptr, 16);
                            if (endptr == hex_digits + 4) {  // successful parsing
                                // Convert Unicode code point to UTF-8
                                if (code_point <= 0x7F) {
                                    stringbuf_append_char(str_buf, (char)code_point);
                                } else if (code_point <= 0x7FF) {
                                    stringbuf_append_char(str_buf, 0xC0 | (code_point >> 6));
                                    stringbuf_append_char(str_buf, 0x80 | (code_point & 0x3F));
                                } else if (code_point <= 0xFFFF) {
                                    stringbuf_append_char(str_buf, 0xE0 | (code_point >> 12));
                                    stringbuf_append_char(str_buf, 0x80 | ((code_point >> 6) & 0x3F));
                                    stringbuf_append_char(str_buf, 0x80 | (code_point & 0x3F));
                                }
                            } else {
                                log_error("Invalid Unicode escape sequence: %.*s", escape_len, escape_start);
                                // append as-is on error
                                stringbuf_append_str_n(str_buf, escape_start, escape_len);
                            }
                        }
                        else if (escape_len > 4 && escape_start[2] == '{') {
                            // \u{...} format (variable length hex digits)
                            const char* hex_start = escape_start + 3;
                            const char* hex_end = escape_start + escape_len - 1;  // should be '}'
                            if (hex_end > hex_start && *hex_end == '}') {
                                int hex_len = hex_end - hex_start;
                                char* hex_str = (char*)malloc(hex_len + 1);
                                memcpy(hex_str, hex_start, hex_len);
                                hex_str[hex_len] = '\0';
                                char* endptr;
                                uint32_t code_point = strtoul(hex_str, &endptr, 16);
                                if (endptr == hex_str + hex_len && code_point <= 0x10FFFF) {
                                    // Convert Unicode code point to UTF-8
                                    if (code_point <= 0x7F) {
                                        stringbuf_append_char(str_buf, (char)code_point);
                                    } else if (code_point <= 0x7FF) {
                                        stringbuf_append_char(str_buf, 0xC0 | (code_point >> 6));
                                        stringbuf_append_char(str_buf, 0x80 | (code_point & 0x3F));
                                    } else if (code_point <= 0xFFFF) {
                                        stringbuf_append_char(str_buf, 0xE0 | (code_point >> 12));
                                        stringbuf_append_char(str_buf, 0x80 | ((code_point >> 6) & 0x3F));
                                        stringbuf_append_char(str_buf, 0x80 | (code_point & 0x3F));
                                    } else {
                                        // 4-byte UTF-8 encoding for code points > 0xFFFF
                                        stringbuf_append_char(str_buf, 0xF0 | (code_point >> 18));
                                        stringbuf_append_char(str_buf, 0x80 | ((code_point >> 12) & 0x3F));
                                        stringbuf_append_char(str_buf, 0x80 | ((code_point >> 6) & 0x3F));
                                        stringbuf_append_char(str_buf, 0x80 | (code_point & 0x3F));
                                    }
                                } else {
                                    log_error("Invalid Unicode escape sequence: %.*s", escape_len, escape_start);
                                    // append as-is on error
                                    stringbuf_append_str_n(str_buf, escape_start, escape_len);
                                }
                                free(hex_str);
                            } else {
                                log_error("Malformed Unicode escape sequence: %.*s", escape_len, escape_start);
                                stringbuf_append_str_n(str_buf, escape_start, escape_len);
                            }
                        } else {
                            log_error("Invalid Unicode escape sequence length: %.*s", escape_len, escape_start);
                            stringbuf_append_str_n(str_buf, escape_start, escape_len);
                        }
                        break;
                    default:
                        // unknown escape sequence, append as-is
                        log_warn("Unknown escape sequence: \\%c", escape_char);
                        stringbuf_append_str_n(str_buf, escape_start, escape_len);
                        break;
                    }
                } else {
                    // malformed escape sequence
                    log_error("Malformed escape sequence: %.*s", escape_len, escape_start);
                    stringbuf_append_str_n(str_buf, escape_start, escape_len);
                }
            }
            else {
                log_error("Unknown string child symbol: %d", symbol);
            }
            child = ts_node_next_named_sibling(child);
        }
        // convert StringBuf to String
        str = stringbuf_to_string(str_buf);
        log_debug("final string: %.*s", str->len, str->chars);
        str_type->string = str;
    }
    // add to const list
    arraylist_append(tp->const_list, str);
    str_type->const_index = tp->const_list->length - 1;
    return (Type*)str_type;
}

Type* build_lit_datetime(Transpiler* tp, TSNode node, TSSymbol symbol) {
    int start = ts_node_start_byte(node), end = ts_node_end_byte(node);
    int datetime_len = end - start;

    TypeDateTime* dt_type = (TypeDateTime*)alloc_type(tp->pool, LMD_TYPE_DTIME, sizeof(TypeDateTime));
    dt_type->is_const = 1;  dt_type->is_literal = 1;

    // Use tp->source string directly
    const char* datetime_start = tp->source + start;
    // Parse the DateTime string directly using ast_pool without allocating datetime_str
    char* parse_end = NULL;
    DateTime* dt = datetime_parse(tp->pool, datetime_start, DATETIME_PARSE_LAMBDA, &parse_end);

    // Check if parsing was successful
    // On success: dt != NULL and parse_end > datetime_start (parsing progressed)
    // On error: dt == NULL and parse_end == datetime_start (no progress)
    if (dt && parse_end > datetime_start) {
        log_debug("parsed datetime fields: %d, %d, %d, %d, %d",
            dt->year_month, dt->day, dt->hour, dt->minute, dt->second);
    }
    else {
        // Fallback to default if parsing fails
        log_debug("Failed to parse datetime: %.*s, using default", datetime_len, datetime_start);
        return NULL;
    }

    dt_type->datetime = *dt;

    // Add to const list
    arraylist_append(tp->const_list, &dt_type->datetime);
    dt_type->const_index = tp->const_list->length - 1;
    log_debug("build lit datetime: %.*s, type: %d", datetime_len, datetime_start, dt_type->type_id);
    return (Type*)dt_type;
}

Type* build_lit_int64(Transpiler* tp, TSNode node) {
    TypeInt64* item_type = (TypeInt64*)alloc_type(tp->pool, LMD_TYPE_INT64, sizeof(TypeInt64));
    StrView source = ts_node_source(tp, node);
    char* endptr;
    int64_t value = strtoll(source.str, &endptr, 10);
    item_type->int64_val = value;
    arraylist_append(tp->const_list, &item_type->int64_val);
    item_type->const_index = tp->const_list->length - 1;
    item_type->is_const = 1;  item_type->is_literal = 1;
    return (Type*)item_type;
}

Type* build_lit_float(Transpiler* tp, TSNode node) {
    TypeFloat* item_type = (TypeFloat*)alloc_type(tp->pool, LMD_TYPE_FLOAT, sizeof(TypeFloat));
    // C supports inf and nan
    log_debug("build lit float");
    const char* num_str = tp->source + ts_node_start_byte(node);
    // check if there's sign
    bool has_sign = false;
    if (num_str[0] == '-') { has_sign = true;  num_str++; } // skip the sign
    while (*num_str == ' ' || *num_str == '\t' || *num_str == '\n' || *num_str == '\r') { num_str++; } // skip leading spaces
    // atof() is not able to handle 'inf', 'nan' on Windows using old MSVC runtime
    log_debug("build lit float: %s", num_str);
    // add special handling for "inf", "-inf", "nan"
    if (strncasecmp(num_str, "inf", 3) == 0) {
        item_type->double_val = INFINITY;
        log_debug("build lit float: inf");
        if (has_sign) { item_type->double_val = -item_type->double_val; }
    }
    else if (strncasecmp(num_str, "nan", 3) == 0) {
        item_type->double_val = NAN;
        log_debug("build lit float: nan");
    }
    else { // normal float parsing
        item_type->double_val = atof(num_str);
        log_debug("build lit float: %s, value: %f", num_str, item_type->double_val);
        if (has_sign) { item_type->double_val = -item_type->double_val; }
    }
    arraylist_append(tp->const_list, &item_type->double_val);
    item_type->const_index = tp->const_list->length - 1;
    item_type->is_const = 1;  item_type->is_literal = 1;
    return (Type*)item_type;
}

mpd_t* str_to_decimal(char* str, mpd_context_t* ctx);

Type* build_lit_decimal(Transpiler* tp, TSNode node) {
    TypeDecimal* item_type = (TypeDecimal*)alloc_type(tp->pool, LMD_TYPE_DECIMAL, sizeof(TypeDecimal));
    StrView num_sv = ts_node_source(tp, node);
    char* num_str = strview_to_cstr(&num_sv);
    // num_str may not end with 'n' or 'N'
    if (num_str[num_sv.length - 1] == 'n' || num_str[num_sv.length - 1] == 'N') {
        num_str[num_sv.length - 1] = '\0';  // clear suffix 'n'/'N'
    }
    log_debug("build lit decimal: %s", num_str);

    // Allocate heap-allocated Decimal structure
    Decimal* decimal;
    decimal = (Decimal*)pool_alloc(tp->pool, sizeof(Decimal));
    item_type->decimal = decimal;

    // Initialize the decimal with reference counting and libmpdec
    decimal->ref_cnt = 1;
    // Use transpiler's decimal context
    decimal->dec_val = str_to_decimal(num_str, tp->decimal_ctx);
    if (!decimal->dec_val) {
        log_error("Error: Failed to parse decimal: %s", num_str);
        free(num_str);
        return &TYPE_ERROR;
    }

    // Add to const list
    arraylist_append(tp->const_list, item_type->decimal);
    item_type->const_index = tp->const_list->length - 1;
    item_type->is_const = 1;  item_type->is_literal = 1;
    free(num_str);
    return (Type*)item_type;
}

AstNode* build_primary_expr(Transpiler* tp, TSNode pri_node) {
    log_debug("*** DEBUG: build_primary_expr called ***");
    AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, pri_node, sizeof(AstPrimaryNode));
    TSNode child = ts_node_named_child(pri_node, 0);
    if (ts_node_is_null(child)) { return (AstNode*)ast_node; }

    // infer data type
    TSSymbol symbol = ts_node_symbol(child);
    log_debug("*** DEBUG: symbol=%d ***", symbol);
    if (symbol == SYM_NULL) {
        ast_node->type = &LIT_NULL;
    }
    else if (symbol == SYM_TRUE || symbol == SYM_FALSE) {
        ast_node->type = &LIT_BOOL;
    }
    else if (symbol == SYM_INT) {
        // Parse the integer value to determine if it fits in 32-bit or needs 64-bit
        StrView source = ts_node_source(tp, child);
        // Create a null-terminated string for strtoll
        char* num_str = (char*)malloc(source.length + 1);
        memcpy(num_str, source.str, source.length);
        num_str[source.length] = '\0';

        char* endptr;
        errno = 0;
        int64_t value = strtoll(num_str, &endptr, 10);
        free(num_str);

        log_debug("build_primary_expr SYM_INT: parsed value %lld", value);
        // Check if the value fits in 56-bit signed integer range
        if (errno == ERANGE || value < INT56_MIN || value > INT56_MAX) {
            // promote to float for values outside int56 range
            log_debug("promote int to float (value outside int56 range)");
            ast_node->type = build_lit_float(tp, child);
        }
        else {
            ast_node->type = &LIT_INT;
        }
    }
    else if (symbol == SYM_DECIMAL) {
        ast_node->type = build_lit_decimal(tp, child);
    }
    else if (symbol == SYM_FLOAT) {
        ast_node->type = build_lit_float(tp, child);
    }
    else if (symbol == SYM_STRING || symbol == SYM_SYMBOL || symbol == SYM_BINARY) {
        ast_node->type = build_lit_string(tp, child, symbol);
    }
    else if (symbol == SYM_DATETIME || symbol == SYM_TIME) {
        ast_node->type = build_lit_datetime(tp, child, symbol);
    }
    else if (symbol == SYM_IDENT) {
        ast_node->expr = build_identifier(tp, child);
        if (ast_node->expr->type->is_const) {
            // replicate the type to remove is_const flag - todo: fast path for const ident
            ast_node->type = alloc_type(tp->pool, ast_node->expr->type->type_id, sizeof(Type));
        }
        else {
            ast_node->type = ast_node->expr->type;
        }
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
    else if (symbol == SYM_CALL_EXPR) { // || symbol == SYM_SYS_FUNC
        ast_node->expr = build_call_expr(tp, child, symbol);
        ast_node->type = ast_node->expr->type;
    }
    else { // from _parenthesized_expr
        ast_node->expr = build_expr(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    log_debug("end build primary expr");
    return (AstNode*)ast_node;
}

AstNode* build_unary_expr(Transpiler* tp, TSNode bi_node) {
    log_debug("build unary expr");
    AstUnaryNode* ast_node = (AstUnaryNode*)alloc_ast_node(tp, AST_NODE_UNARY, bi_node, sizeof(AstUnaryNode));
    TSNode op_node = ts_node_child_by_field_id(bi_node, FIELD_OPERATOR);
    StrView op = ts_node_source(tp, op_node);
    ast_node->op_str = op;
    if (strview_equal(&op, "not")) { ast_node->op = OPERATOR_NOT; }
    else if (strview_equal(&op, "-")) { ast_node->op = OPERATOR_NEG; }
    else if (strview_equal(&op, "+")) { ast_node->op = OPERATOR_POS; }

    TSNode operand_node = ts_node_child_by_field_id(bi_node, FIELD_OPERAND);
    ast_node->operand = build_expr(tp, operand_node);

    // Defensive validation: ensure operand was built successfully
    if (!ast_node->operand) {
        log_error("Error: build_unary_expr failed to build operand");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // Additional validation: ensure operand has valid type
    if (!ast_node->operand->type) {
        log_error("Error: build_unary_expr operand missing type information");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // More robust type inference based on operator and operand type
    TypeId operand_type = ast_node->operand->type->type_id;
    TypeId type_id;

    if (ast_node->op == OPERATOR_NOT) {
        type_id = LMD_TYPE_BOOL;
    }
    else if (ast_node->op == OPERATOR_POS || ast_node->op == OPERATOR_NEG) {
        // For numeric unary operators (+/-), preserve the operand type if numeric
        if (LMD_TYPE_INT <= operand_type && operand_type <= LMD_TYPE_NUMBER) {
            type_id = operand_type;  // Preserve the exact numeric type
        }
        else {
            type_id = LMD_TYPE_ANY;  // Non-numeric types need runtime handling
        }
    }
    else {
        log_error("Error: build_unary_expr unknown operator");
        type_id = LMD_TYPE_ANY;  // Default fallback
    }

    ast_node->type = alloc_type(tp->pool, type_id, sizeof(Type));

    log_debug("end build unary expr");
    return (AstNode*)ast_node;
}

AstNode* build_binary_expr(Transpiler* tp, TSNode bi_node) {
    log_debug("build binary expr");
    AstBinaryNode* ast_node = (AstBinaryNode*)alloc_ast_node(tp, AST_NODE_BINARY, bi_node, sizeof(AstBinaryNode));
    TSNode left_node = ts_node_child_by_field_id(bi_node, FIELD_LEFT);
    ast_node->left = build_expr(tp, left_node);

    // Defensive validation: ensure left operand was built successfully
    if (!ast_node->left) {
        log_error("Error: build_binary_expr failed to build left operand");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    TSNode op_node = ts_node_child_by_field_id(bi_node, FIELD_OPERATOR);
    StrView op = ts_node_source(tp, op_node);
    ast_node->op_str = op;
    if (strview_equal(&op, "and")) { ast_node->op = OPERATOR_AND; }
    else if (strview_equal(&op, "or")) { ast_node->op = OPERATOR_OR; }
    else if (strview_equal(&op, "+")) { ast_node->op = OPERATOR_ADD; }
    else if (strview_equal(&op, "++")) { ast_node->op = OPERATOR_JOIN; }
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
    else {
        log_error("Error: build_binary_expr unknown operator: %.*s", (int)op.length, op.str);
        ast_node->op = OPERATOR_ADD; // Default fallback to prevent crashes
    }

    TSNode right_node = ts_node_child_by_field_id(bi_node, FIELD_RIGHT);
    ast_node->right = build_expr(tp, right_node);

    // Defensive validation: ensure right operand was built successfully
    if (!ast_node->right) {
        log_error("Error: build_binary_expr failed to build right operand");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // Additional validation: ensure both operands have valid types
    if (!ast_node->left->type || !ast_node->right->type) {
        log_error("Error: build_binary_expr operands missing type information");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    TypeId left_type = ast_node->left->type->type_id, right_type = ast_node->right->type->type_id;
    log_debug("left type: %d, right type: %d", left_type, right_type);
    TypeId type_id;
    if (ast_node->op == OPERATOR_DIV) {
        if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_FLOAT) {
            // If either operand is decimal, result is decimal
            // if (left_type == LMD_TYPE_DECIMAL || right_type == LMD_TYPE_DECIMAL) {
            //     type_id = LMD_TYPE_DECIMAL;
            // } else {
            type_id = LMD_TYPE_FLOAT;  // division and power produce float results for non-decimals
            // }
        }
        else {
            type_id = LMD_TYPE_ANY;
        }
    }
    else if (ast_node->op == OPERATOR_ADD) {
        if (left_type == right_type && (left_type == LMD_TYPE_ARRAY || left_type == LMD_TYPE_LIST)) {
            type_id = left_type;
        }
        else if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_FLOAT) {
            // If either operand is decimal, result is decimal
            // if (left_type == LMD_TYPE_DECIMAL || right_type == LMD_TYPE_DECIMAL) {
            //     type_id = LMD_TYPE_DECIMAL;
            // } else {
            type_id = std::max(left_type, right_type);
            // }
        }
        else {
            type_id = LMD_TYPE_ANY;
        }
    }
    else if (ast_node->op == OPERATOR_SUB || ast_node->op == OPERATOR_MUL) {
        if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_FLOAT) {
            // If either operand is decimal, result is decimal
            // if (left_type == LMD_TYPE_DECIMAL || right_type == LMD_TYPE_DECIMAL) {
            //     type_id = LMD_TYPE_DECIMAL;
            // } else {
            type_id = std::max(left_type, right_type);
            //}
        }
        else {
            type_id = LMD_TYPE_ANY;
        }
    }
    else if (ast_node->op == OPERATOR_AND || ast_node->op == OPERATOR_OR) {
        type_id = LMD_TYPE_ANY;  // based on truthy idiom, not simple logic and/or
    }
    else if (ast_node->op == OPERATOR_EQ || ast_node->op == OPERATOR_NE ||
        ast_node->op == OPERATOR_LT || ast_node->op == OPERATOR_LE ||
        ast_node->op == OPERATOR_GT || ast_node->op == OPERATOR_GE ||
        ast_node->op == OPERATOR_IS || ast_node->op == OPERATOR_IN) {
        type_id = LMD_TYPE_BOOL;
    }
    else if (ast_node->op == OPERATOR_IDIV) {
        if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_NUMBER &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_NUMBER) {
            type_id = LMD_TYPE_INT;  // Integer division always produces int results
        }
        else {
            type_id = LMD_TYPE_ANY;
        }
    }
    else if (ast_node->op == OPERATOR_TO) {
        type_id = LMD_TYPE_RANGE;
    }
    else {  // OPERATOR_JOIN, etc.
        type_id = LMD_TYPE_ANY;
    }
    ast_node->type = alloc_type(tp->pool, type_id, sizeof(Type));
    log_debug("end build binary expr");
    return (AstNode*)ast_node;
}

AstNode* build_if_expr(Transpiler* tp, TSNode if_node) {
    log_debug("build if expr");
    AstIfNode* ast_node = (AstIfNode*)alloc_ast_node(tp, AST_NODE_IF_EXPR, if_node, sizeof(AstIfNode));
    TSNode cond_node = ts_node_child_by_field_id(if_node, FIELD_COND);
    ast_node->cond = build_expr(tp, cond_node);

    // Defensive validation: ensure condition was built successfully
    if (!ast_node->cond) {
        log_error("Error: build_if_expr failed to build condition expression");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    TSNode then_node = ts_node_child_by_field_id(if_node, FIELD_THEN);
    ast_node->then = build_expr(tp, then_node);

    // Defensive validation: ensure then clause was built successfully
    if (!ast_node->then) {
        log_error("Error: build_if_expr failed to build then expression");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    TSNode else_node = ts_node_child_by_field_id(if_node, FIELD_ELSE);
    if (ts_node_is_null(else_node)) {
        ast_node->otherwise = NULL;
    }
    else {
        ast_node->otherwise = build_expr(tp, else_node);
        // Defensive validation: if else node exists, ensure it was built successfully
        if (!ast_node->otherwise) {
            log_error("Error: build_if_expr failed to build else expression");
            ast_node->type = &TYPE_ERROR;
            return (AstNode*)ast_node;
        }
    }

    // Additional validation: ensure expressions have valid types
    if (!ast_node->cond->type || !ast_node->then->type ||
        (ast_node->otherwise && !ast_node->otherwise->type)) {
        log_error("Error: build_if_expr expressions missing type information");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // determine the type of the if expression, should be union of then and else
    TypeId then_type_id = ast_node->then->type->type_id;
    TypeId else_type_id = ast_node->otherwise ? ast_node->otherwise->type->type_id : LMD_TYPE_NULL;

    // Check if branches have incompatible types that require coercion to ANY
    bool need_any_type = false;
    if (then_type_id != else_type_id) {
        // check if number coercion is possible
        // if (then_type_id == LMD_TYPE_INT && else_type_id == LMD_TYPE_FLOAT ||
        //     then_type_id == LMD_TYPE_FLOAT && else_type_id == LMD_TYPE_INT) {
        //     // coercion is possible
        // } else {
            // Incompatible types that cannot be coerced, use ANY
        log_warn("Error: incompatible types %d and %d, coercing to ANY", then_type_id, else_type_id);
        need_any_type = true;
        // }
    }

    TypeId type_id = need_any_type ? LMD_TYPE_ANY : std::max(then_type_id, else_type_id);
    ast_node->type = alloc_type(tp->pool, type_id, sizeof(Type));
    log_debug("end build if expr");
    return (AstNode*)ast_node;
}

AstNode* build_if_stam(Transpiler* tp, TSNode if_node) {
    log_debug("build if stam");
    AstIfNode* ast_node = (AstIfNode*)alloc_ast_node(tp, AST_NODE_IF_STAM, if_node, sizeof(AstIfNode));

    TSNode cond_node = ts_node_child_by_field_id(if_node, FIELD_COND);
    ast_node->cond = build_expr(tp, cond_node);

    // Defensive validation: ensure condition was built successfully
    if (!ast_node->cond) {
        log_error("Error: build_if_stam failed to build condition expression");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    TSNode then_node = ts_node_child_by_field_id(if_node, FIELD_THEN);
    ast_node->then = build_expr(tp, then_node);

    // Defensive validation: ensure then clause was built successfully
    if (!ast_node->then) {
        log_error("Error: build_if_stam failed to build then expression");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    TSNode else_node = ts_node_child_by_field_id(if_node, FIELD_ELSE);
    if (ts_node_is_null(else_node)) {
        ast_node->otherwise = NULL;  // optional for IF statements
    }
    else {
        ast_node->otherwise = build_expr(tp, else_node);
    }

    // Additional validation: ensure expressions have valid types
    if (!ast_node->cond->type || !ast_node->then->type ||
        (ast_node->otherwise && !ast_node->otherwise->type)) {
        log_error("Error: build_if_stam expressions missing type information");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }

    // if statement return type
    ast_node->type = ast_node->otherwise && ast_node->otherwise->type &&
        ast_node->otherwise->type->type_id == ast_node->then->type->type_id ? ast_node->then->type : &TYPE_ANY;
    log_debug("end build if stam");
    return (AstNode*)ast_node;
}

AstNode* build_list(Transpiler* tp, TSNode list_node) {
    log_debug("build list");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_LIST, list_node, sizeof(AstListNode));
    TypeList* type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_LIST, sizeof(TypeList));
    ast_node->list_type = type;
    ast_node->type = &TYPE_ANY;  // list returns Item, not List

    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    tp->current_scope = ast_node->vars;

    TSNode child = ts_node_named_child(list_node, 0);
    AstNode* prev_declare = NULL, * prev_item = NULL;
    while (!ts_node_is_null(child)) {
        log_debug("build_list: processing child");
        AstNode* item = build_expr(tp, child);
        if (item) {
            log_debug("build_list: got item with node_type %d", item->node_type);
            if (item->node_type == AST_NODE_ASSIGN) {
                AstNode* declare = item;
                log_debug("got declare type %d", declare->node_type);
                if (prev_declare == NULL) {
                    ast_node->declare = declare;
                }
                else {
                    prev_declare->next = declare;
                }
                prev_declare = declare;
            }
            else { // normal list item
                log_debug("build_list: adding item as list item, incrementing length from %ld to %ld", type->length, type->length + 1);
                if (!prev_item) {
                    ast_node->item = item;
                }
                else {
                    prev_item->next = item;
                }
                prev_item = item;
                type->length++;
            }
        }
        else {
            log_debug("build_list: got null item");
        }
        child = ts_node_next_named_sibling(child);
    }
    if (!ast_node->declare && type->length == 1) {
        tp->current_scope = ast_node->vars->parent;  // Fix scope restoration
        log_debug("build_list: returning single item with type %d", ast_node->item->type->type_id);
        return ast_node->item;
    }
    tp->current_scope = ast_node->vars->parent;
    return (AstNode*)ast_node;
}

AstNode* build_assign_expr(Transpiler* tp, TSNode asn_node, bool is_type_definition) {
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_ASSIGN, asn_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(asn_node, FIELD_NAME);
    int start_byte = ts_node_start_byte(name);
    StrView name_view = { .str = tp->source + start_byte, .length = ts_node_end_byte(name) - start_byte };
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);

    TSNode type_node = ts_node_child_by_field_id(asn_node, FIELD_TYPE);
    TSNode val_node = ts_node_child_by_field_id(asn_node, FIELD_AS);

    // handle type definitions vs variable assignments differently
    if (is_type_definition) {
        // for type statements: type Name = TypeExpr
        // build the type expression - the result's type field is a TypeType* wrapper
        if (ts_node_is_null(val_node)) {
            log_error("type definition: missing type expression");
            ast_node->type = &TYPE_ANY;
            ast_node->as = nullptr;
        } else {
            AstNode* type_expr = build_expr(tp, val_node);
            ast_node->as = type_expr;
            if (type_expr && type_expr->type) {
                ast_node->type = type_expr->type;  // Keep as TypeType* wrapper
            } else {
                log_warn("type definition: failed to build type expression");
                ast_node->type = &TYPE_ANY;
            }
        }
    } else {
        // for variable assignments: let name = value or let name: type = value
        if (ts_node_is_null(val_node)) {
            log_error("assignment: missing value expression");
            ast_node->as = nullptr;
            ast_node->type = &TYPE_ANY;
        } else {
            ast_node->as = build_expr(tp, val_node);

            // determine the type of the variable
            if (ts_node_is_null(type_node)) {
                ast_node->type = ast_node->as->type;
            }
            else {
                AstNode* type_expr = build_expr(tp, type_node);
                ast_node->type = ((TypeType*)type_expr->type)->type;
            }
        }
    }

    // push the name to the name stack
    push_name(tp, ast_node, NULL);
    return (AstNode*)ast_node;
}

AstNode* build_let_expr(Transpiler* tp, TSNode let_node) {
    TSNode type_node = ts_node_child_by_field_id(let_node, FIELD_DECLARE);
    return build_assign_expr(tp, type_node, false);  // let expressions are not type definitions
}

AstNode* build_let_and_type_stam(Transpiler* tp, TSNode let_node, TSSymbol symbol) {
    AstLetNode* ast_node = (AstLetNode*)alloc_ast_node(tp,
        symbol == SYM_TYPE_DEFINE ? AST_NODE_TYPE_STAM :
        symbol == SYM_PUB_STAM ? AST_NODE_PUB_STAM : AST_NODE_LET_STAM, let_node, sizeof(AstLetNode));

    // determine if this is a type definition based on the parent symbol
    bool is_type_definition = (symbol == SYM_TYPE_DEFINE);

    // 'let' can have multiple name-value declarations
    TSTreeCursor cursor = ts_tree_cursor_new(let_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode* prev_declare = NULL;
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            TSSymbol child_symbol = ts_node_symbol(child);
            // defensive check: validate symbol type
            // type_assign is aliased as assign_expr in the grammar, so we only see SYM_ASSIGN_EXPR
            if (child_symbol != SYM_ASSIGN_EXPR) {
                log_error("Error: build_let_and_type_stam expected SYM_ASSIGN_EXPR but got symbol %d", child_symbol);
                // skip invalid node and continue - defensive recovery
                has_node = ts_tree_cursor_goto_next_sibling(&cursor);
                continue;
            }
            AstNode* declare = build_assign_expr(tp, child, is_type_definition);
            // additional defensive check
            if (!declare) {
                log_error("Error: build_let_and_type_stam failed to build assign expression");
                has_node = ts_tree_cursor_goto_next_sibling(&cursor);
                continue;
            }
            if (prev_declare == NULL) {
                ast_node->declare = declare;
            }
            else {
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

StrView build_key_string(Transpiler* tp, TSNode key_node) {
    log_debug("build key string");
    TSSymbol symbol = ts_node_symbol(key_node);
    switch (symbol) {
        // todo: handle string and symbol escape
    case SYM_SYMBOL:  case SYM_STRING: {
        int start_byte = ts_node_start_byte(key_node) + 1; // skip the first quote
        int end_byte = ts_node_end_byte(key_node) - 1; // skip the last quote
        return (StrView) { .str = tp->source + start_byte, .length = static_cast<size_t>(end_byte - start_byte) };
    }
    case SYM_IDENT: {
        return (StrView)ts_node_source(tp, key_node);
    }
    default:
        log_debug("unknown key type %d", symbol);
        return (StrView) { .str = NULL, .length = 0 };
    }
}

AstNamedNode* build_key_expr(Transpiler* tp, TSNode pair_node) {
    log_debug("build_key_expr");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_KEY_EXPR, pair_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(pair_node, FIELD_NAME);
    if (ts_node_is_null(name)) {
        log_error("build_key_expr: missing name field");
        ast_node->name = name_pool_create_strview(tp->name_pool, (StrView){.str = "", .length = 0});
        ast_node->type = &TYPE_ANY;
        ast_node->as = nullptr;
        return ast_node;
    }

    StrView name_view = build_key_string(tp, name);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);

    TSNode val_node = ts_node_child_by_field_id(pair_node, FIELD_AS);
    if (ts_node_is_null(val_node)) {
        log_error("build_key_expr: missing value field");
        ast_node->type = &TYPE_ANY;
        ast_node->as = nullptr;
        return ast_node;
    }

    log_debug("build key as");
    ast_node->as = build_expr(tp, val_node);

    // determine the type of the field
    ast_node->type = ast_node->as->type;
    return ast_node;
}

AstNode* build_base_type(Transpiler* tp, TSNode type_node) {
    AstTypeNode* ast_node = (AstTypeNode*)alloc_ast_node(tp, AST_NODE_TYPE, type_node, sizeof(AstTypeNode));
    log_debug("build type annotation");
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
        log_debug("unknown base type %.*s", (int)type_name.length, type_name.str);
        ast_node->type = (Type*)&LIT_TYPE_ERROR;
    }
    log_debug("built base type %.*s, type_id %d", (int)type_name.length, type_name.str,
        ((TypeType*)ast_node->type)->type->type_id);
    return (AstNode*)ast_node;
}

AstNode* build_list_type(Transpiler* tp, TSNode list_node) {
    log_debug("build list type");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_LIST_TYPE, list_node, sizeof(AstListNode));
    TypeType* node_type = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    ast_node->type = node_type;
    TypeList* type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_LIST, sizeof(TypeList));
    node_type->type = (Type*)type;  ast_node->list_type = type;

    TSNode child = ts_node_named_child(list_node, 0);
    AstNode* prev_declare = NULL, * prev_item = NULL;
    while (!ts_node_is_null(child)) {
        AstNode* item = build_expr(tp, child);
        if (item) {
            if (!prev_item) {
                ast_node->item = item;
            }
            else {
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
    log_debug("build array type");
    AstArrayNode* ast_node = (AstArrayNode*)alloc_ast_node(tp, AST_NODE_ARRAY_TYPE, array_node, sizeof(AstArrayNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeArray* type = (TypeArray*)alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
    ((TypeType*)ast_node->type)->type = (Type*)type;

    TSNode child = ts_node_named_child(array_node, 0);
    AstNode* prev_item = NULL;  Type* nested_type = NULL;
    while (!ts_node_is_null(child)) {
        AstNode* item = build_expr(tp, child);
        if (item) {
            if (!prev_item) {
                ast_node->item = item;  nested_type = item->type;
            }
            else {
                prev_item->next = item;
                if (nested_type && item->type->type_id != nested_type->type_id) {
                    nested_type = NULL;  // type mismatch, reset the nested type to NULL
                }
            }
            prev_item = item;
            type->length++;
        }
        child = ts_node_next_named_sibling(child);
    }
    type->nested = nested_type;

    arraylist_append(tp->type_list, ast_node->type);
    type->type_index = tp->type_list->length - 1;
    return (AstNode*)ast_node;
}

AstNode* build_map_type(Transpiler* tp, TSNode map_node) {
    AstMapNode* ast_node = (AstMapNode*)alloc_ast_node(tp, AST_NODE_MAP_TYPE, map_node, sizeof(AstMapNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeMap* type = (TypeMap*)alloc_type(tp->pool, LMD_TYPE_MAP, sizeof(TypeMap));
    ((TypeType*)ast_node->type)->type = (Type*)type;

    TSNode child = ts_node_named_child(map_node, 0);
    AstNode* prev_item = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    // map type does not support dynamic expr in the body
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        // Skip comments in map type definition
        if (symbol == SYM_COMMENT) {
            child = ts_node_next_named_sibling(child);
            continue;
        }

        AstNode* item = (AstNode*)build_key_expr(tp, child);
        if (item && ((AstNamedNode*)item)->name) {
            if (!prev_item) { ast_node->item = item; }
            else { prev_item->next = item; }
            prev_item = item;

            ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
            // Convert pooled String* to StrView* for shape entry
            String* pooled_name = ((AstNamedNode*)item)->name;
            StrView* name_view = (StrView*)pool_calloc(tp->pool, sizeof(StrView));
            name_view->str = pooled_name->chars;
            name_view->length = pooled_name->len;
            shape_entry->name = name_view;
            shape_entry->type = item->type;
            shape_entry->byte_offset = byte_offset;
            if (!prev_entry) { type->shape = shape_entry; }
            else { prev_entry->next = shape_entry; }
            prev_entry = shape_entry;
            type->length++;  byte_offset += sizeof(void*);
        }
        child = ts_node_next_named_sibling(child);
    }
    type->byte_size = byte_offset;

    arraylist_append(tp->type_list, ast_node->type);
    type->type_index = tp->type_list->length - 1;
    return (AstNode*)ast_node;
}

AstNode* build_content_type(Transpiler* tp, TSNode list_node) {
    log_debug("build content type");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_CONTENT_TYPE, list_node, sizeof(AstListNode));
    TypeList* type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_LIST, sizeof(TypeList));
    ast_node->type = ast_node->list_type = type;

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
    log_debug("end building content type: %ld", type->length);
    return ast_node;
}

AstNode* build_element_type(Transpiler* tp, TSNode elmt_node) {
    log_debug("build element type");
    AstElementNode* ast_node = (AstElementNode*)alloc_ast_node(tp,
        AST_NODE_ELMT_TYPE, elmt_node, sizeof(AstElementNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeElmt* type = (TypeElmt*)alloc_type(tp->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    ((TypeType*)ast_node->type)->type = (Type*)type;

    TSNode child = ts_node_named_child(elmt_node, 0);
    AstNode* prev_item = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        if (symbol == SYM_COMMENT) {} // skip comments
        else if (symbol == SYM_IDENT) {  // element name
            StrView name = ts_node_source(tp, child);
            String* pooled_name = name_pool_create_strview(tp->name_pool, name);
            // Convert pooled String* to StrView for TypeElmt
            type->name.str = pooled_name->chars;
            type->name.length = pooled_name->len;
        }
        else if (symbol == SYM_CONTENT_TYPE) {  // element content
            ast_node->content = build_content_type(tp, child);
        }
        else {  // attrs
            AstNode* item = (AstNode*)build_key_expr(tp, child);
            if (!prev_item) { ast_node->item = item; }
            else { prev_item->next = item; }
            prev_item = item;

            ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
            // Convert pooled String* to StrView* for shape entry
            String* pooled_name = ((AstNamedNode*)item)->name;
            StrView* name_view = (StrView*)pool_calloc(tp->pool, sizeof(StrView));
            name_view->str = pooled_name->chars;
            name_view->length = pooled_name->len;
            shape_entry->name = name_view;
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
    log_debug("build fn type");
    AstFuncNode* ast_node = (AstFuncNode*)alloc_ast_node(tp, AST_NODE_FUNC_TYPE, func_node, sizeof(AstFuncNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeFunc* fn_type = (TypeFunc*)alloc_type(tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
    ((TypeType*)ast_node->type)->type = (Type*)fn_type;

    // build the params
    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    tp->current_scope = ast_node->vars;
    TSTreeCursor cursor = ts_tree_cursor_new(func_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNamedNode* prev_param = NULL;  int param_count = 0;
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {  // param declaration
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNamedNode* param = build_param_expr(tp, child, true);
            log_debug("got param type %d", param->node_type);
            if (prev_param == NULL) {
                ast_node->param = param;
                fn_type->param = (TypeParam*)param->type;
            }
            else {
                prev_param->next = (AstNode*)param;
                ((TypeParam*)prev_param->type)->next = (TypeParam*)param->type;
            }
            prev_param = param;  param_count++;
        }
        else if (field_id == FIELD_TYPE) {  // return type
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode* type_expr = build_expr(tp, child);
            fn_type->returned = ((TypeType*)type_expr->type)->type;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    fn_type->param_count = param_count;

    arraylist_append(tp->type_list, ast_node->type);
    fn_type->type_index = tp->type_list->length - 1;
    log_debug("func type index: %d", fn_type->type_index);
    return (AstNode*)ast_node;
}

AstNode* build_primary_type(Transpiler* tp, TSNode type_node) {
    log_debug("build primary type");
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
        case SYM_COMMENT:
            break; // skip comments
        default: // literal values
            return build_expr(tp, child);
        }
        child = ts_node_next_named_sibling(child);
    }
    return NULL;
}

AstNode* build_binary_type(Transpiler* tp, TSNode bi_node) {
    log_debug("build binary type");
    AstBinaryNode* ast_node = (AstBinaryNode*)alloc_ast_node(tp,
        AST_NODE_BINARY_TYPE, bi_node, sizeof(AstBinaryNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeBinary* type = (TypeBinary*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeBinary));
    ((TypeType*)ast_node->type)->type = (Type*)type;

    TSNode left_node = ts_node_child_by_field_id(bi_node, FIELD_LEFT);
    ast_node->left = build_expr(tp, left_node);

    TSNode op_node = ts_node_child_by_field_id(bi_node, FIELD_OPERATOR);
    StrView op = ts_node_source(tp, op_node);
    ast_node->op_str = op;
    if (strview_equal(&op, "|")) { ast_node->op = OPERATOR_UNION; }
    else if (strview_equal(&op, "&")) { ast_node->op = OPERATOR_OR; }
    else if (strview_equal(&op, "!")) { ast_node->op = OPERATOR_EXCLUDE; }
    log_debug("unknown operator: %.*s", (int)op.length, op.str);

    TSNode right_node = ts_node_child_by_field_id(bi_node, FIELD_RIGHT);
    ast_node->right = build_expr(tp, right_node);

    type->left = ast_node->left->type;
    type->right = ast_node->right->type;
    type->op = ast_node->op;
    arraylist_append(tp->type_list, ast_node->type);
    type->type_index = tp->type_list->length - 1;
    log_debug("binary type index: %d", type->type_index);
    return (AstNode*)ast_node;
}

AstNode* build_occurrence_type(Transpiler* tp, TSNode occurrence_node) {
    log_debug("build occurrence type");
    AstUnaryNode* ast_node = (AstUnaryNode*)alloc_ast_node(tp, AST_NODE_UNARY_TYPE, occurrence_node, sizeof(AstUnaryNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeUnary* type = (TypeUnary*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeUnary));
    ((TypeType*)ast_node->type)->type = (Type*)type;

    TSNode op_node = ts_node_child_by_field_id(occurrence_node, FIELD_OPERATOR);
    StrView op = ts_node_source(tp, op_node);
    ast_node->op_str = op;
    if (strview_equal(&op, "?")) { ast_node->op = OPERATOR_OPTIONAL; }
    else if (strview_equal(&op, "+")) { ast_node->op = OPERATOR_ONE_MORE; }
    else if (strview_equal(&op, "*")) { ast_node->op = OPERATOR_ZERO_MORE; }
    log_debug("unknown operator: %.*s", (int)op.length, op.str);

    TSNode operand_node = ts_node_child_by_field_id(occurrence_node, FIELD_OPERAND);
    ast_node->operand = build_expr(tp, operand_node);
    type->operand = ast_node->operand->type;

    // log_debug("built occurrence type with modifier '%c' for base type %d", modifier, node_type->type->type_id);
    return (AstNode*)ast_node;
}

// todo: build reference type

AstNode* build_map(Transpiler* tp, TSNode map_node) {
    AstMapNode* ast_node = (AstMapNode*)alloc_ast_node(tp, AST_NODE_MAP, map_node, sizeof(AstMapNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_MAP, sizeof(TypeMap));
    TypeMap* type = (TypeMap*)ast_node->type;

    TSNode child = ts_node_named_child(map_node, 0);
    AstNode* prev_item = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        if (symbol == SYM_COMMENT) {  // skip comments
            child = ts_node_next_named_sibling(child);  continue;
        }
        // named map item, or dynamic map expr
        AstNode* item = (symbol == SYM_MAP_ITEM) ? (AstNode*)build_key_expr(tp, child) : build_expr(tp, child);
        if (!item) { log_error("build_map: null expr item");  break; }
        if (!prev_item) { ast_node->item = item; }
        else { prev_item->next = item; }
        prev_item = item;

        ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
        if (symbol == SYM_MAP_ITEM) {
            // convert pooled String* to StrView* for shape entry
            String* pooled_name = ((AstNamedNode*)item)->name;
            StrView* name_view = (StrView*)pool_calloc(tp->pool, sizeof(StrView));
            name_view->str = pooled_name->chars;
            name_view->length = pooled_name->len;
            shape_entry->name = name_view;
        }
        else {
            shape_entry->name = NULL;
        }
        shape_entry->type = item->type;
        if (!shape_entry->name && !(item->type->type_id == LMD_TYPE_MAP || item->type->type_id == LMD_TYPE_ANY)) {
            log_error("invalid map item type %d, should be map or any", item->type->type_id);
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
    log_debug("build element expr");
    AstElementNode* ast_node = (AstElementNode*)alloc_ast_node(tp,
        AST_NODE_ELEMENT, elmt_node, sizeof(AstElementNode));
    TypeElmt* type = (TypeElmt*)alloc_type(tp->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    ast_node->type = (Type*)type;

    TSNode child = ts_node_named_child(elmt_node, 0);
    AstNode* prev_item = NULL;  ShapeEntry* prev_entry = NULL;  int byte_offset = 0;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        if (symbol == SYM_COMMENT) {} // skip comments
        else if (symbol == SYM_IDENT) {  // element name
            StrView name = ts_node_source(tp, child);
            String* pooled_name = name_pool_create_strview(tp->name_pool, name);
            // Convert pooled String* to StrView for TypeElmt
            type->name.str = pooled_name->chars;
            type->name.length = pooled_name->len;
        }
        else if (symbol == SYM_CONTENT) {  // element content
            ast_node->content = build_content(tp, child, false, false);
        }
        else {  // attrs
            AstNode* item = (symbol == SYM_ATTR) ? (AstNode*)build_key_expr(tp, child) : build_expr(tp, child);
            if (!prev_item) { ast_node->item = item; }
            else { prev_item->next = item; }
            prev_item = item;

            ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(tp->pool, sizeof(ShapeEntry));
            if (symbol == SYM_ATTR) {
                // Convert pooled String* to StrView* for shape entry
                String* pooled_name = ((AstNamedNode*)item)->name;
                StrView* name_view = (StrView*)pool_calloc(tp->pool, sizeof(StrView));
                name_view->str = pooled_name->chars;
                name_view->length = pooled_name->len;
                shape_entry->name = name_view;
            }
            else {
                shape_entry->name = NULL;
            }
            shape_entry->type = item->type;
            if (!shape_entry->name && !(item->type->type_id == LMD_TYPE_MAP || item->type->type_id == LMD_TYPE_ANY)) {
                log_debug("invalid map item type %d, should be map or any", item->type->type_id);
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
    type->content_length = ast_node->content ?
        (ast_node->content->node_type == AST_NODE_CONTENT ? ((AstListNode*)ast_node->content)->list_type->length : 1) : 0;
    return (AstNode*)ast_node;
}

AstNode* build_loop_expr(Transpiler* tp, TSNode loop_node) {
    log_debug("build loop expr");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_LOOP, loop_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(loop_node, FIELD_NAME);
    int start_byte = ts_node_start_byte(name);
    StrView name_view = { .str = tp->source + start_byte, .length = ts_node_end_byte(name) - start_byte };
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);

    TSNode expr_node = ts_node_child_by_field_id(loop_node, FIELD_AS);
    ast_node->as = build_expr(tp, expr_node);

    // determine the type of the variable
    Type* expr_type = ast_node->as->type;
    if (expr_type->type_id == LMD_TYPE_ARRAY || expr_type->type_id == LMD_TYPE_LIST) {
        // Safely determine nested type
        TypeArray* array_type = (TypeArray*)expr_type;
        // Validate that the cast is safe by checking the nested pointer
        if (array_type && array_type->nested && (uintptr_t)array_type->nested > 0x1000) {
            ast_node->type = array_type->nested;
        }
        else {
            log_debug("Warning: Invalid nested type in array during loop AST building, using TYPE_ANY");
            ast_node->type = &TYPE_ANY;
        }
    }
    else if (expr_type->type_id == LMD_TYPE_RANGE) {
        ast_node->type = &TYPE_INT;
    }
    else {
        ast_node->type = expr_type;
    }

    // push the name to the name stack
    push_name(tp, ast_node, NULL);
    return (AstNode*)ast_node;
}

AstNode* build_for_expr(Transpiler* tp, TSNode for_node) {
    log_debug("build for expr");
    AstForNode* ast_node = (AstForNode*)alloc_ast_node(tp, AST_NODE_FOR_EXPR, for_node, sizeof(AstForNode));
    // Type will be determined after processing the 'then' expression

    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    tp->current_scope = ast_node->vars;
    // for can have multiple loop declarations
    TSTreeCursor cursor = ts_tree_cursor_new(for_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode* prev_loop = NULL;
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode* loop = build_loop_expr(tp, child);
            log_debug("got loop type %d", loop->node_type);
            if (prev_loop == NULL) {
                ast_node->loop = loop;
            }
            else {
                prev_loop->next = loop;
            }
            prev_loop = loop;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    if (!ast_node->loop) {
        log_error("Error: missing for loop declare");
    }

    TSNode then_node = ts_node_child_by_field_id(for_node, FIELD_THEN);
    ast_node->then = build_expr(tp, then_node);

    // determine for-expr type
    if (!ast_node->then) {
        log_debug("missing for then");
        ast_node->type = &TYPE_ERROR;  // fallback
    }
    else {
        log_debug("got for then type %d", ast_node->then->node_type);
        // For expression type should be Item | List containing the element type
        // TypeList* type_list = (TypeList*)alloc_type(tp->pool, LMD_TYPE_LIST, sizeof(TypeList));
        // type_list->nested = ast_node->then->type;
        ast_node->type = &TYPE_ANY;
    }

    tp->current_scope = ast_node->vars->parent;
    return (AstNode*)ast_node;
}

AstNode* build_for_stam(Transpiler* tp, TSNode for_node) {
    log_debug("build for stam");
    AstForNode* ast_node = (AstForNode*)alloc_ast_node(tp, AST_NODE_FOR_STAM, for_node, sizeof(AstForNode));

    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    tp->current_scope = ast_node->vars;

    // for can have multiple loop declarations
    TSTreeCursor cursor = ts_tree_cursor_new(for_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode* prev_loop = NULL;
    while (has_node) {
        // Check if the current node's field ID matches the target field ID
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode* loop = build_loop_expr(tp, child);
            log_debug("got loop type %d", loop->node_type);
            if (prev_loop == NULL) {
                ast_node->loop = loop;
            }
            else {
                prev_loop->next = loop;
            }
            prev_loop = loop;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    TSNode then_node = ts_node_child_by_field_id(for_node, FIELD_THEN);
    ast_node->then = build_expr(tp, then_node);
    log_debug("got for then type %d", ast_node->then->node_type);

    // for statement returns type
    ast_node->type = &TYPE_ANY;
    tp->current_scope = ast_node->vars->parent;
    return (AstNode*)ast_node;
}

AstNamedNode* build_param_expr(Transpiler* tp, TSNode param_node, bool is_type) {
    log_debug("build param expr");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_PARAM, param_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(param_node, FIELD_NAME);
    StrView name_str = ts_node_source(tp, name);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_str);

    TSNode type_node = ts_node_child_by_field_id(param_node, FIELD_TYPE);
    // determine the type of the field
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(TypeParam));
    if (!ts_node_is_null(type_node)) {
        AstNode* type_expr = build_expr(tp, type_node);
        *ast_node->type = *((TypeType*)type_expr->type)->type;
    }
    else {
        *ast_node->type = ast_node->as ? *ast_node->as->type : TYPE_ANY;
    }

    if (!is_type) { push_name(tp, ast_node, NULL); }
    return ast_node;
}

// for both func expr and stam
AstNode* build_func(Transpiler* tp, TSNode func_node, bool is_named, bool is_global) {
    log_debug("build function");
    bool is_proc = false;
    TSNode kind = ts_node_child_by_field_id(func_node, FIELD_KIND);
    if (!ts_node_is_null(kind)) {
        StrView kind_str = ts_node_source(tp, kind);
        is_proc = strview_equal(&kind_str, "pn");
    }
    log_debug("is proc: %d", is_proc);

    AstFuncNode* ast_node = (AstFuncNode*)alloc_ast_node(tp,
        is_proc ? AST_NODE_PROC : is_named ? AST_NODE_FUNC : AST_NODE_FUNC_EXPR,
        func_node, sizeof(AstFuncNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
    TypeFunc* fn_type = (TypeFunc*)ast_node->type;
    fn_type->is_anonymous = !is_named;  fn_type->is_proc = is_proc;

    // 'pub' flag
    TSNode pub = ts_node_child_by_field_id(func_node, FIELD_PUB);
    fn_type->is_public = !ts_node_is_null(pub);

    // get the function name
    if (is_named) {
        TSNode fn_name_node = ts_node_child_by_field_id(func_node, FIELD_NAME);
        StrView name = ts_node_source(tp, fn_name_node);
        ast_node->name = name_pool_create_strview(tp->name_pool, name);
        // add fn name to current scope
        push_name(tp, (AstNamedNode*)ast_node, NULL);
    }

    // build the params
    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    ast_node->vars->is_proc = is_proc;
    tp->current_scope = ast_node->vars;
    TSTreeCursor cursor = ts_tree_cursor_new(func_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNamedNode* prev_param = NULL;  int param_count = 0;
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {  // param declaration
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNamedNode* param = build_param_expr(tp, child, false);
            log_debug("got param type %d", param->node_type);
            if (prev_param == NULL) {
                ast_node->param = param;
                fn_type->param = (TypeParam*)param->type;
            }
            else {
                prev_param->next = (AstNode*)param;
                ((TypeParam*)prev_param->type)->next = (TypeParam*)param->type;
            }
            prev_param = param;  param_count++;
        }
        else if (field_id == FIELD_TYPE) {  // return type
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode* type_expr = build_expr(tp, child);
            fn_type->returned = ((TypeType*)type_expr->type)->type;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    fn_type->param_count = param_count;

    // build the function body
    // ast_node->locals = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    // ast_node->locals->parent = tp->current_scope;
    // tp->current_scope = ast_node->locals;
    TSNode fn_body_node = ts_node_child_by_field_id(func_node, FIELD_BODY);
    ast_node->body = build_expr(tp, fn_body_node);

    // determine the function return type
    if (!fn_type->returned) {
        fn_type->returned = ast_node->body->type;
    } else if (ast_node->body->type) {
        // Validate declared return type against body type
        if (!types_compatible(ast_node->body->type, fn_type->returned)) {
            int line = ts_node_start_point(func_node).row + 1;
            record_type_error(tp, line, 
                "function '%.*s' body returns type %d, declared return type %d",
                (int)ast_node->name->len, ast_node->name->chars,
                ast_node->body->type->type_id, fn_type->returned->type_id);
            // Keep declared return type for interface consistency
        }
    }

    // restore parent namescope
    tp->current_scope = ast_node->vars->parent;
    log_debug("end building fn");
    return (AstNode*)ast_node;
}

AstNode* build_content(Transpiler* tp, TSNode list_node, bool flattern, bool is_global) {
    log_debug("build content");
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_CONTENT, list_node, sizeof(AstListNode));
    TypeList* type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_LIST, sizeof(TypeList));
    ast_node->list_type = type;
    ast_node->type = &TYPE_ANY;  // content() returns Item, not List

    // build body content
    TSNode child = ts_node_named_child(list_node, 0);
    AstNode* prev_item = NULL;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        AstNode* item = symbol == SYM_FUNC_STAM || symbol == SYM_FUNC_EXPR_STAM ?
            build_func(tp, child, true, is_global) : build_expr(tp, child);
        if (item) {
            if (!prev_item) {
                ast_node->item = item;
            }
            else {
                prev_item->next = item;
            }
            prev_item = item;
            type->length++;
        }
        // else comment or error
        child = ts_node_next_named_sibling(child);
    }
    log_debug("end building content item: %p, %ld", ast_node->item, type->length);
    if (flattern && type->length == 1) { return ast_node->item; }
    return ast_node;
}

AstNode* build_lit_node(Transpiler* tp, TSNode lit_node, bool quoted_value, TSSymbol symbol) {
    log_debug("build lit node");
    // string, symbol, binary, datetime, time
    AstPrimaryNode* ast_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, lit_node, sizeof(AstPrimaryNode));
    // TSNode val_node = quoted_value ? ts_node_named_child(lit_node, 0) : lit_node;
    ast_node->type = build_lit_string(tp, lit_node, symbol);
    return (AstNode*)ast_node;
}

AstNode* build_expr(Transpiler* tp, TSNode expr_node) {
    // get the function name
    TSSymbol symbol = ts_node_symbol(expr_node);
    log_debug("build_expr: %s", ts_node_type(expr_node));
    switch (symbol) {
    case SYM_PRIMARY_EXPR:
        return build_primary_expr(tp, expr_node);
    case SYM_UNARY_EXPR:
        return build_unary_expr(tp, expr_node);
    case SYM_BINARY_EXPR:
        return build_binary_expr(tp, expr_node);
    case SYM_LET_EXPR:
        return build_let_expr(tp, expr_node);
    case SYM_LET_STAM:  case SYM_PUB_STAM:  case SYM_TYPE_DEFINE:
        return build_let_and_type_stam(tp, expr_node, symbol);
    case SYM_FOR_EXPR:
        return build_for_expr(tp, expr_node);
    case SYM_FOR_STAM:
        return build_for_stam(tp, expr_node);
    case SYM_IF_EXPR:
        return build_if_expr(tp, expr_node);
    case SYM_IF_STAM:
        return build_if_stam(tp, expr_node);
    case SYM_ASSIGN_EXPR:
        return build_assign_expr(tp, expr_node, false);  // standalone assign_expr is not a type definition
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
    case SYM_TRUE:  case SYM_FALSE: {
        AstPrimaryNode* b_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        b_node->type = &LIT_BOOL;
        return (AstNode*)b_node;
    }
    case SYM_INT: {
        AstPrimaryNode* i_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));

        // Parse the integer value to determine if it fits in 32-bit or needs 64-bit
        StrView source = ts_node_source(tp, expr_node);

        // Create a null-terminated string for strtoll
        char* num_str = (char*)malloc(source.length + 1);
        memcpy(num_str, source.str, source.length);
        num_str[source.length] = '\0';

        char* endptr;
        int64_t value = strtoll(num_str, &endptr, 10);
        free(num_str);

        log_debug("SYM_INT: parsed value %lld, checking range", value);
        // Check if the value fits in 56-bit signed integer range
        if (INT56_MIN <= value && value <= INT56_MAX) {
            log_debug("Using LIT_INT for value %lld", value);
            i_node->type = &LIT_INT;
        }
        else { // promote to float for values outside int56 range
            log_debug("Using float for value %lld (outside int56 range)", value);
            i_node->type = build_lit_float(tp, expr_node);
        }
        return (AstNode*)i_node;
    }
    case SYM_FLOAT: {
        AstPrimaryNode* f_node = (AstPrimaryNode*)alloc_ast_node(tp, AST_NODE_PRIMARY, expr_node, sizeof(AstPrimaryNode));
        f_node->type = build_lit_float(tp, expr_node);
        return (AstNode*)f_node;
    }
    case SYM_BASE_TYPE:
        return build_base_type(tp, expr_node);
    case SYM_PRIMARY_TYPE:
        return build_primary_type(tp, expr_node);
    case SYM_BINARY_TYPE:
        return build_binary_type(tp, expr_node);
    case SYM_TYPE_OCCURRENCE:
        return build_occurrence_type(tp, expr_node);
    case SYM_IMPORT_MODULE:
        // already processed
        return NULL;
    case SYM_COMMENT:
        return NULL;
    case SYM_INDEX:
        // This is likely a parsing error - index tokens should not appear as standalone expressions
        // Common cause: malformed syntax like "1..3" which parses as "1." + ".3"
        log_error("Error: Unexpected index token - check for malformed range syntax (use 'to' instead of '..')");
        return NULL;
    default:
        log_debug("unknown syntax node: %s", ts_node_type(expr_node));
        return NULL;
    }
}

void declare_module_import(Transpiler* tp, AstImportNode* import_node) {
    log_debug("declare_module_import");
    // import module
    if (!import_node->script) { log_error("Missing script");  return; }
    log_debug("script reference: %s", import_node->script->reference);
    // loop through the public functions in the module
    if (!import_node->script->ast_root) { log_error("Missing AST root");  return; }
    AstNode* node = import_node->script->ast_root;
    // Defensive check: validate node type instead of using assert
    if (node->node_type != AST_SCRIPT) {
        log_error("Error: declare_module_import expected AST_SCRIPT but got node_type %d", node->node_type);
        return;  // Defensive recovery - exit gracefully
    }
    node = ((AstScript*)node)->child;
    while (node) {
        if (node->node_type == AST_NODE_CONTENT) {
            node = ((AstListNode*)node)->item;  // drill down
            continue;
        }
        else if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR || node->node_type == AST_NODE_PROC) {
            AstFuncNode* func_node = (AstFuncNode*)node;
            log_debug("got imported fn/pn: %.*s, is_public: %d", (int)func_node->name->len, func_node->name->chars,
                ((TypeFunc*)func_node->type)->is_public);
            if (((TypeFunc*)func_node->type)->is_public) {
                push_name(tp, (AstNamedNode*)func_node, import_node);
            }
        }
        else if (node->node_type == AST_NODE_PUB_STAM) {
            AstLetNode* pub_node = (AstLetNode*)node;
            AstNode* declare = pub_node->declare;
            while (declare) {
                AstNamedNode* dec_node = (AstNamedNode*)declare;
                push_name(tp, (AstNamedNode*)dec_node, import_node);
                log_debug("got pub var: %.*s", (int)dec_node->name->len, dec_node->name->chars);
                declare = declare->next;
            }
        }
        node = node->next;
    }
}

AstNode* build_module_import(Transpiler* tp, TSNode import_node) {
    log_debug("build_module_import");
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
            ast_node->alias = name_pool_create_strview(tp->name_pool, alias);
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
            log_debug("runtime: %p", tp->runtime);
            log_debug("runtime dir: %s", tp->runtime->current_dir);
            StrBuf* buf = strbuf_new();
            strbuf_append_format(buf, "%s%.*s", tp->runtime->current_dir,
                (int)ast_node->module.length - 1, ast_node->module.str + 1);
            char* ch = buf->str + buf->length - (ast_node->module.length - 1);
            while (*ch) { if (*ch == '.') *ch = '/';  ch++; }
            strbuf_append_str(buf, ".ls");

            #ifdef SIMPLE_SCHEMA_PARSER
            // Skip module loading in simple schema parser mode
            ast_node->script = nullptr;
            #else
            ast_node->script = load_script(tp->runtime, buf->str, NULL);
            #endif

            strbuf_free(buf);
            // import names/definitions from the modules
            if (ast_node->script) {
                declare_module_import(tp, ast_node);
            }
            else {
                log_error("Error: failed to load module %.*s", (int)ast_node->module.length, ast_node->module.str);
            }
        }
        else {
            log_debug("module type not supported yet: %.*s", (int)ast_node->module.length, ast_node->module.str);
        }
    }
    return (AstNode*)ast_node;
}

AstNode* build_script(Transpiler* tp, TSNode script_node) {
    log_debug("build script");
    AstScript* ast_node = (AstScript*)alloc_ast_node(tp, AST_SCRIPT, script_node, sizeof(AstScript));
    tp->current_scope = ast_node->global_vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));

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
            log_debug("unknown script child: %s", ts_node_type(child));
        }
        if (ast) {
            if (!prev) ast_node->child = ast;
            else { prev->next = ast; }
            prev = ast;
        }
        child = ts_node_next_named_sibling(child);
    }
    if (ast_node->child) ast_node->type = ast_node->child->type;
    log_debug("build script child: %p", ast_node->child);
    return (AstNode*)ast_node;
}
