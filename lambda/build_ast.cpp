#include "transpiler.hpp"
#include "lambda-decimal.hpp"
#include "lambda-error.h"
#include "../lib/hashmap.h"
#include "../lib/datetime.h"
#include "../lib/log.h"
#include "../lib/arraylist.h"
#include <errno.h>
#include <algorithm>  // for std::max

AstNamedNode* build_param_expr(Transpiler* tp, TSNode param_node, bool is_type);
AstNode* build_occurrence_type(Transpiler* tp, TSNode occurrence_node);
AstNode* build_named_argument(Transpiler* tp, TSNode arg_node);

// Forward declarations for pattern building
AstNode* build_pattern_expr(Transpiler* tp, TSNode node);
AstNode* build_string_pattern(Transpiler* tp, TSNode node, bool is_symbol);
AstNode* build_lit_node(Transpiler* tp, TSNode lit_node, bool quoted_value, TSSymbol symbol);
AstNode* build_identifier(Transpiler* tp, TSNode ident_node);

// Forward declarations for pipe expression building
AstNode* build_current_item(Transpiler* tp, TSNode node);
AstNode* build_current_index(Transpiler* tp, TSNode node);

// System function definitions with method call support
// Format: {fn_id, name, arg_count, return_type, is_proc, is_overloaded, is_method_eligible, first_param_type}
// is_method_eligible: true if can be called as obj.method() style
// first_param_type: expected type of first param for method calls (LMD_TYPE_ANY for any type)
SysFuncInfo sys_funcs[] = {
    // type/conversion functions - all method-eligible
    {SYSFUNC_LEN, "len", 1, &TYPE_INT64, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_TYPE, "type", 1, &TYPE_TYPE, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_INT, "int", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_INT64, "int64", 1, &TYPE_INT64, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_FLOAT, "float", 1, &TYPE_FLOAT, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_DECIMAL, "decimal", 1, &TYPE_DECIMAL, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_BINARY, "binary", 1, &TYPE_BINARY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_NUMBER, "number", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_STRING, "string", 1, &TYPE_STRING, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_SYMBOL, "symbol", 1, &TYPE_SYMBOL, false, false, true, LMD_TYPE_ANY},
    // datetime functions - date/time with 1 arg are method-eligible
    {SYSFUNC_DATETIME, "datetime", 1, &TYPE_DTIME, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_DATE, "date", 1, &TYPE_DTIME, false, false, true, LMD_TYPE_DTIME},
    {SYSFUNC_TIME, "time", 1, &TYPE_DTIME, false, false, true, LMD_TYPE_DTIME},
    {SYSFUNC_JUSTNOW, "justnow", 0, &TYPE_DTIME, false, false, false, LMD_TYPE_ANY},  // no args
    // collection functions
    {SYSFUNC_SET, "set", -1, &TYPE_ANY, false, false, false, LMD_TYPE_ANY},  // variable args, not method-eligible
    {SYSFUNC_SLICE, "slice", 3, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},  // slice(obj, start, end) -> obj.slice(start, end)
    {SYSFUNC_ALL, "all", 1, &TYPE_BOOL, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_ANY, "any", 1, &TYPE_BOOL, false, false, true, LMD_TYPE_ANY},
    // min/max - 1-arg is method-eligible, 2-arg is not (no clear "self")
    {SYSFUNC_MIN1, "min", 1, &TYPE_ANY, false, true, true, LMD_TYPE_ANY},
    {SYSFUNC_MIN2, "min", 2, &TYPE_ANY, false, true, false, LMD_TYPE_ANY},  // min(a,b) - no clear receiver
    {SYSFUNC_MAX1, "max", 1, &TYPE_ANY, false, true, true, LMD_TYPE_ANY},
    {SYSFUNC_MAX2, "max", 2, &TYPE_ANY, false, true, false, LMD_TYPE_ANY},  // max(a,b) - no clear receiver
    // aggregation functions - all method-eligible on collections
    {SYSFUNC_SUM, "sum", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_AVG, "avg", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    // math functions - method-eligible on numbers
    {SYSFUNC_ABS, "abs", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_ROUND, "round", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_FLOOR, "floor", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_CEIL, "ceil", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    // I/O functions
    {SYSFUNC_INPUT1, "input", 1, &TYPE_ANY, false, true, false, LMD_TYPE_ANY},  // not method-eligible
    {SYSFUNC_INPUT2, "input", 2, &TYPE_ANY, false, true, false, LMD_TYPE_ANY},
    {SYSFUNC_FORMAT1, "format", 1, &TYPE_STRING, false, true, true, LMD_TYPE_ANY},
    {SYSFUNC_FORMAT2, "format", 2, &TYPE_STRING, false, true, true, LMD_TYPE_ANY},
    {SYSFUNC_ERROR, "error", 1, &TYPE_ERROR, false, false, false, LMD_TYPE_ANY},  // not method-eligible
    // string functions - method-eligible on strings
    {SYSFUNC_NORMALIZE, "normalize", 1, &TYPE_STRING, false, true, true, LMD_TYPE_STRING},
    {SYSFUNC_NORMALIZE2, "normalize", 2, &TYPE_STRING, false, true, true, LMD_TYPE_STRING},
    {SYSFUNC_CONTAINS, "contains", 2, &TYPE_BOOL, false, false, true, LMD_TYPE_STRING},
    {SYSFUNC_STARTS_WITH, "starts_with", 2, &TYPE_BOOL, false, false, true, LMD_TYPE_STRING},
    {SYSFUNC_ENDS_WITH, "ends_with", 2, &TYPE_BOOL, false, false, true, LMD_TYPE_STRING},
    {SYSFUNC_INDEX_OF, "index_of", 2, &TYPE_INT64, false, false, true, LMD_TYPE_STRING},
    {SYSFUNC_LAST_INDEX_OF, "last_index_of", 2, &TYPE_INT64, false, false, true, LMD_TYPE_STRING},
    {SYSFUNC_TRIM, "trim", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING},
    {SYSFUNC_TRIM_START, "trim_start", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING},
    {SYSFUNC_TRIM_END, "trim_end", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING},
    {SYSFUNC_SPLIT, "split", 2, &TYPE_ANY, false, false, true, LMD_TYPE_STRING},
    {SYSFUNC_STR_JOIN, "str_join", 2, &TYPE_STRING, false, false, true, LMD_TYPE_ANY},  // arr.str_join(sep)
    {SYSFUNC_REPLACE, "replace", 3, &TYPE_ANY, false, false, true, LMD_TYPE_STRING},
    // vector/array functions - method-eligible on arrays
    {SYSFUNC_PROD, "prod", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_CUMSUM, "cumsum", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_CUMPROD, "cumprod", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_ARGMIN, "argmin", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_ARGMAX, "argmax", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_FILL, "fill", 2, &TYPE_ANY, false, false, false, LMD_TYPE_ANY},  // fill(n, val) - n is count, not self
    {SYSFUNC_DOT, "dot", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},  // a.dot(b)
    {SYSFUNC_NORM, "norm", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    // statistical functions - method-eligible on collections
    {SYSFUNC_MEAN, "mean", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_MEDIAN, "median", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_VARIANCE, "variance", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_DEVIATION, "deviation", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    // element-wise math functions - method-eligible on numbers/arrays
    {SYSFUNC_SQRT, "sqrt", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_LOG, "log", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_LOG10, "log10", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_EXP, "exp", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_SIN, "sin", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_COS, "cos", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_TAN, "tan", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_SIGN, "sign", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    // vector manipulation functions - method-eligible on collections
    {SYSFUNC_REVERSE, "reverse", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_SORT, "sort", 1, &TYPE_ANY, false, true, true, LMD_TYPE_ANY},
    {SYSFUNC_SORT2, "sort", 2, &TYPE_ANY, false, true, true, LMD_TYPE_ANY},  // arr.sort(comparator)
    {SYSFUNC_UNIQUE, "unique", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},
    {SYSFUNC_CONCAT, "concat", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},  // a.concat(b)
    {SYSFUNC_TAKE, "take", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},  // arr.take(n)
    {SYSFUNC_DROP, "drop", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},  // arr.drop(n)
    {SYSFUNC_ZIP, "zip", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},  // a.zip(b)
    {SYSFUNC_RANGE3, "range", 3, &TYPE_ANY, false, true, false, LMD_TYPE_ANY},  // range(s,e,step) - constructor, not method
    {SYSFUNC_QUANTILE, "quantile", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY},  // arr.quantile(p)
    // variadic parameter access - not method-eligible
    {SYSFUNC_VARG0, "varg", 0, &TYPE_ANY, false, true, false, LMD_TYPE_ANY},
    {SYSFUNC_VARG1, "varg", 1, &TYPE_ANY, false, true, false, LMD_TYPE_ANY},
    // procedural functions - not method-eligible (side effects)
    {SYSPROC_NOW, "now", 0, &TYPE_DTIME, true, false, false, LMD_TYPE_ANY},
    {SYSPROC_TODAY, "today", 0, &TYPE_DTIME, true, false, false, LMD_TYPE_ANY},
    {SYSPROC_PRINT, "print", 1, &TYPE_NULL, true, false, false, LMD_TYPE_ANY},
    {SYSPROC_FETCH, "fetch", 2, &TYPE_ANY, true, false, false, LMD_TYPE_ANY},
    {SYSPROC_OUTPUT2, "output", 2, &TYPE_NULL, true, true, false, LMD_TYPE_ANY},
    {SYSPROC_OUTPUT3, "output", 3, &TYPE_NULL, true, true, false, LMD_TYPE_ANY},
    {SYSPROC_CMD, "cmd", 2, &TYPE_ANY, true, false, false, LMD_TYPE_ANY},
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

// Look up a system function for method-style call: obj.method(args)
// This searches for a sys func where arg_count includes the object (+1)
// and validates that the function is method-eligible and type-compatible
SysFuncInfo* get_sys_func_for_method(StrView* method_name, int method_arg_count, TypeId obj_type_id) {
    // method_arg_count is the count of arguments in parentheses (not including obj)
    // sys func arg_count includes the object, so we add 1
    int total_arg_count = method_arg_count + 1;
    
    for (size_t i = 0; i < sizeof(sys_funcs) / sizeof(sys_funcs[0]); i++) {
        SysFuncInfo* info = &sys_funcs[i];
        if (strview_equal(method_name, info->name) && info->arg_count == total_arg_count) {
            // Check if this function is method-eligible
            if (!info->is_method_eligible) {
                log_debug("method_call sys func '%.*s' not method-eligible", 
                    (int)method_name->length, method_name->str);
                return NULL;
            }
            
            // Check type compatibility with first parameter
            if (info->first_param_type != LMD_TYPE_ANY && obj_type_id != LMD_TYPE_ANY) {
                if (info->first_param_type != obj_type_id) {
                    // Additional check for numeric types
                    bool type_compatible = false;
                    if (info->first_param_type == LMD_TYPE_NUMBER) {
                        type_compatible = (obj_type_id == LMD_TYPE_INT || 
                                          obj_type_id == LMD_TYPE_INT64 ||
                                          obj_type_id == LMD_TYPE_FLOAT ||
                                          obj_type_id == LMD_TYPE_DECIMAL);
                    }
                    if (!type_compatible) {
                        log_debug("method_call type mismatch for '%.*s': expected %d, got %d", 
                            (int)method_name->length, method_name->str, 
                            info->first_param_type, obj_type_id);
                        return NULL;
                    }
                }
            }
            
            log_debug("method_call found sys func: %.*s, args=%d", 
                (int)method_name->length, method_name->str, total_arg_count);
            return info;
        }
    }
    log_debug("method_call no sys func: %.*s, args=%d", 
        (int)method_name->length, method_name->str, total_arg_count);
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
    
    // Format error message
    char error_msg[512];
    va_list args;
    va_start(args, format);
    vsnprintf(error_msg, sizeof(error_msg), format, args);
    va_end(args);
    
    // Create structured error
    SourceLocation loc = src_loc(tp->reference, line, 1);
    loc.source = tp->source;
    LambdaError* error = err_create(ERR_TYPE_MISMATCH, error_msg, &loc);
    
    // Store in error list if available
    if (tp->errors) {
        arraylist_append(tp->errors, error);
    }
    
    // Also log for backward compatibility
    log_error("type_error (line %d): %s", line, error_msg);
    
    // Check threshold
    if (tp->error_count >= tp->max_errors) {
        log_error("error_threshold: max errors (%d) reached", tp->max_errors);
    }
}

// Record a semantic error with error code
void record_semantic_error(Transpiler* tp, TSNode node, LambdaErrorCode code, const char* format, ...) {
    tp->error_count++;
    
    // Get location from TSNode
    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);
    
    // Format error message
    char error_msg[512];
    va_list args;
    va_start(args, format);
    vsnprintf(error_msg, sizeof(error_msg), format, args);
    va_end(args);
    
    // Create structured error with span
    SourceLocation loc = src_loc_span(tp->reference, 
        start.row + 1, start.column + 1,
        end.row + 1, end.column + 1);
    loc.source = tp->source;
    LambdaError* error = err_create(code, error_msg, &loc);
    
    // Store in error list if available
    if (tp->errors) {
        arraylist_append(tp->errors, error);
    }
    
    // Also log for backward compatibility
    log_error("error[E%d] at %s:%u:%u: %s", code, 
        tp->reference ? tp->reference : "<unknown>",
        start.row + 1, start.column + 1, error_msg);
    
    // Check threshold
    if (tp->error_count >= tp->max_errors) {
        log_error("error_threshold: max errors (%d) reached", tp->max_errors);
    }
}

// Check if should continue transpiling based on error count
bool should_continue_transpiling(Transpiler* tp) {
    return tp->error_count < tp->max_errors;
}

// Forward declaration for closure capture analysis
void collect_captures_from_node(Transpiler* tp, AstNode* node, NameScope* fn_scope, 
                                 NameScope* global_scope, CaptureInfo** captures);

// Check if a scope is an ancestor of another scope
bool is_ancestor_scope(NameScope* ancestor, NameScope* descendant) {
    NameScope* scope = descendant;
    while (scope) {
        if (scope == ancestor) return true;
        scope = scope->parent;
    }
    return false;
}

// Check if a name entry is defined in a scope or its descendants (local to function)
bool is_local_to_scope(NameEntry* entry, NameScope* fn_scope) {
    // Check if entry is in fn_scope or any nested scope within the function
    NameScope* scope = fn_scope;
    while (scope) {
        NameEntry* e = scope->first;
        while (e) {
            if (e == entry) return true;
            e = e->next;
        }
        // Note: we only check the immediate scope, not children
        // This is because fn_scope contains params and local vars
        break;
    }
    return false;
}

// Check if a name entry is in global scope
bool is_global_entry(NameEntry* entry, NameScope* global_scope) {
    if (!global_scope) return false;
    NameEntry* e = global_scope->first;
    while (e) {
        if (e == entry) return true;
        e = e->next;
    }
    return false;
}

// Add a capture to the list if not already present
void add_capture(Transpiler* tp, CaptureInfo** captures, String* name, NameEntry* entry) {
    // Check if already captured
    CaptureInfo* c = *captures;
    while (c) {
        if (c->name == name || (c->name && name && 
            c->name->len == name->len && 
            memcmp(c->name->chars, name->chars, name->len) == 0)) {
            return; // already captured
        }
        c = c->next;
    }
    
    // Add new capture
    CaptureInfo* capture = (CaptureInfo*)pool_calloc(tp->pool, sizeof(CaptureInfo));
    capture->name = name;
    capture->entry = entry;
    capture->is_mutable = false; // TODO: detect mutations
    capture->next = *captures;
    *captures = capture;
    log_debug("capture added: %.*s", (int)name->len, name->chars);
}

// Recursively collect captures from an AST node
void collect_captures_from_node(Transpiler* tp, AstNode* node, NameScope* fn_scope, 
                                 NameScope* global_scope, CaptureInfo** captures) {
    if (!node) return;
    
    switch (node->node_type) {
    case AST_NODE_IDENT: {
        AstIdentNode* ident = (AstIdentNode*)node;
        if (ident->entry && ident->entry->node) {
            // Check if this identifier refers to a variable from an enclosing scope
            // (not local to fn_scope, not global, not an import)
            if (!ident->entry->import && 
                !is_local_to_scope(ident->entry, fn_scope) &&
                !is_global_entry(ident->entry, global_scope)) {
                add_capture(tp, captures, ident->name, ident->entry);
            }
        }
        break;
    }
    case AST_NODE_PRIMARY: {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        collect_captures_from_node(tp, pri->expr, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_UNARY: {
        AstUnaryNode* un = (AstUnaryNode*)node;
        collect_captures_from_node(tp, un->operand, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* bin = (AstBinaryNode*)node;
        collect_captures_from_node(tp, bin->left, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, bin->right, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_IF_EXPR:
    case AST_NODE_IF_STAM: {
        AstIfNode* if_node = (AstIfNode*)node;
        collect_captures_from_node(tp, if_node->cond, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, if_node->then, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, if_node->otherwise, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_FOR_EXPR:
    case AST_NODE_FOR_STAM: {
        AstForNode* for_node = (AstForNode*)node;
        // Note: loop variable is local, handled by fn_scope extension
        AstNode* loop = for_node->loop;
        while (loop) {
            if (loop->node_type == AST_NODE_LOOP) {
                AstNamedNode* loop_var = (AstNamedNode*)loop;
                collect_captures_from_node(tp, loop_var->as, fn_scope, global_scope, captures);
            }
            loop = loop->next;
        }
        collect_captures_from_node(tp, for_node->then, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_WHILE_STAM: {
        AstWhileNode* while_node = (AstWhileNode*)node;
        collect_captures_from_node(tp, while_node->cond, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, while_node->body, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        collect_captures_from_node(tp, call->function, fn_scope, global_scope, captures);
        AstNode* arg = call->argument;
        while (arg) {
            collect_captures_from_node(tp, arg, fn_scope, global_scope, captures);
            arg = arg->next;
        }
        break;
    }
    case AST_NODE_INDEX_EXPR:
    case AST_NODE_MEMBER_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        collect_captures_from_node(tp, field->object, fn_scope, global_scope, captures);
        collect_captures_from_node(tp, field->field, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_LIST:
    case AST_NODE_CONTENT:
    case AST_NODE_ARRAY: {
        AstListNode* list = (AstListNode*)node;
        AstNode* item = list->item;
        while (item) {
            collect_captures_from_node(tp, item, fn_scope, global_scope, captures);
            item = item->next;
        }
        break;
    }
    case AST_NODE_MAP: {
        AstMapNode* map = (AstMapNode*)node;
        AstNode* item = map->item;
        while (item) {
            collect_captures_from_node(tp, item, fn_scope, global_scope, captures);
            item = item->next;
        }
        break;
    }
    case AST_NODE_ASSIGN:
    case AST_NODE_KEY_EXPR:
    case AST_NODE_LOOP: {
        AstNamedNode* named = (AstNamedNode*)node;
        collect_captures_from_node(tp, named->as, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_RETURN_STAM: {
        AstReturnNode* ret = (AstReturnNode*)node;
        collect_captures_from_node(tp, ret->value, fn_scope, global_scope, captures);
        break;
    }
    case AST_NODE_LET_STAM:
    case AST_NODE_VAR_STAM: {
        AstLetNode* let = (AstLetNode*)node;
        AstNode* decl = let->declare;
        while (decl) {
            collect_captures_from_node(tp, decl, fn_scope, global_scope, captures);
            decl = decl->next;
        }
        break;
    }
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC: {
        // Nested functions: we need to propagate any captures that the nested function
        // has which come from scopes ABOVE the current function's scope.
        // This ensures that intermediate functions capture variables needed by their
        // nested functions for closure environment construction.
        AstFuncNode* nested_fn = (AstFuncNode*)node;
        if (nested_fn->captures) {
            CaptureInfo* cap = nested_fn->captures;
            while (cap) {
                // Check if this captured variable is NOT local to the current function's scope
                // and NOT global. If so, the current function also needs to capture it.
                if (cap->entry && !cap->entry->import &&
                    !is_local_to_scope(cap->entry, fn_scope) &&
                    !is_global_entry(cap->entry, global_scope)) {
                    add_capture(tp, captures, cap->name, cap->entry);
                }
                cap = cap->next;
            }
        }
        break;
    }
    default:
        // Other node types don't need capture analysis
        break;
    }
}

// Analyze captures for a function node
void analyze_captures(Transpiler* tp, AstFuncNode* fn_node, NameScope* global_scope) {
    fn_node->captures = nullptr;
    collect_captures_from_node(tp, fn_node->body, fn_node->vars, global_scope, &fn_node->captures);
    
    if (fn_node->captures) {
        log_debug("function %.*s has captures:", 
            fn_node->name ? (int)fn_node->name->len : 5,
            fn_node->name ? fn_node->name->chars : "anon");
        CaptureInfo* c = fn_node->captures;
        while (c) {
            log_debug("  - %.*s", (int)c->name->len, c->name->chars);
            c = c->next;
        }
    }
}

// Find the global scope by walking up the parent chain
NameScope* find_global_scope(NameScope* scope) {
    while (scope && scope->parent) {
        scope = scope->parent;
    }
    return scope;
}

// str_to_decimal is now in lambda-decimal.cpp as decimal_parse_str

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

// check if a name is a reserved type keyword
bool is_type_keyword(StrView name) {
    static const char* type_keywords[] = {
        "null", "any", "error", "bool", "int", "int64", "float", "decimal", "number",
        "date", "time", "datetime", "symbol", "string", "binary",
        "list", "array", "map", "element", "entity", "object", "type", "function"
    };
    for (size_t i = 0; i < sizeof(type_keywords) / sizeof(type_keywords[0]); i++) {
        if (strview_equal(&name, type_keywords[i])) {
            return true;
        }
    }
    return false;
}

// lookup a name in the current scope only (not in parent scopes)
// returns the existing entry if found, NULL otherwise
NameEntry* lookup_name_in_current_scope(Transpiler* tp, String* name) {
    NameEntry* entry = tp->current_scope->first;
    while (entry) {
        if (entry->name == name ||  // pointer comparison (interned strings)
            (entry->name->len == name->len && 
             memcmp(entry->name->chars, name->chars, name->len) == 0)) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

void push_name(Transpiler* tp, AstNamedNode* node, AstImportNode* import) {
    log_debug("pushing name %.*s, %p", (int)node->name->len, node->name->chars, node->type);
    
    // check for duplicate definition in current scope
    NameEntry* existing = lookup_name_in_current_scope(tp, node->name);
    if (existing) {
        record_semantic_error(tp, node->node, ERR_DUPLICATE_DEFINITION,
            "duplicate definition of '%.*s' in the same scope",
            (int)node->name->len, node->name->chars);
        // continue anyway to allow further error checking
    }
    
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
        record_semantic_error(tp, array_node, ERR_SYNTAX_ERROR, 
            "Failed to build field expression - invalid object or field");
        ast_node->type = &TYPE_ERROR;
        return (AstNode*)ast_node;
    }
    // additional safety: check if object has valid type
    if (!ast_node->object->type) {
        record_semantic_error(tp, array_node, ERR_INTERNAL_ERROR,
            "Field expression object missing type information");
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
    TSSymbol fn_symbol = ts_node_symbol(function_node);
    
    // Check if this is a method-style call: obj.method(args)
    // In this case, function_node is a primary_expr containing a member_expr
    bool is_method_call = false;
    TSNode member_node = {0};
    TSNode object_node = {0};
    TSNode method_name_node = {0};
    StrView method_name = {0};
    
    if (fn_symbol == SYM_PRIMARY_EXPR) {
        // Look for member_expr inside primary_expr
        TSNode inner = ts_node_child(function_node, 0);
        if (!ts_node_is_null(inner) && ts_node_symbol(inner) == SYM_MEMBER_EXPR) {
            member_node = inner;
            is_method_call = true;
        }
    } else if (fn_symbol == SYM_MEMBER_EXPR) {
        member_node = function_node;
        is_method_call = true;
    }
    
    if (is_method_call) {
        object_node = ts_node_child_by_field_id(member_node, FIELD_OBJECT);
        method_name_node = ts_node_child_by_field_id(member_node, FIELD_FIELD);
        method_name = ts_node_source(tp, method_name_node);
        log_debug("method_call detected: obj.%.*s() with %d args", 
            (int)method_name.length, method_name.str, arg_count);
    }
    
    // For method calls, first build the object to get its type for validation
    AstNode* method_object = NULL;
    TypeId obj_type_id = LMD_TYPE_ANY;
    if (is_method_call && !ts_node_is_null(object_node)) {
        method_object = build_expr(tp, object_node);
        if (method_object && method_object->type) {
            obj_type_id = method_object->type->type_id;
        }
    }
    
    // Try to resolve as sys func
    StrView func_name = ts_node_source(tp, function_node);
    SysFuncInfo* sys_func_info = NULL;
    
    if (is_method_call) {
        // Try method-style lookup: obj.method(args) -> method(obj, args)
        sys_func_info = get_sys_func_for_method(&method_name, arg_count, obj_type_id);
        if (sys_func_info) {
            log_debug("method_call resolved to sys func: %s", sys_func_info->name);
        }
    }
    
    if (!sys_func_info && !is_method_call) {
        // Traditional function call lookup
        sys_func_info = get_sys_func_info(&func_name, arg_count);
    }
    
    if (sys_func_info) {
        log_debug("build sys call");
        if (sys_func_info->is_proc) {
            if (!tp->current_scope->is_proc) {
                const char* fn_name = is_method_call ? method_name.str : func_name.str;
                int fn_name_len = is_method_call ? (int)method_name.length : (int)func_name.length;
                record_semantic_error(tp, call_node, ERR_PROC_IN_FN,
                    "procedure '%.*s' cannot be called in a function",
                    fn_name_len, fn_name);
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
        
        // For method calls, prepend the object as the first argument
        if (is_method_call && method_object) {
            ast_node->argument = method_object;
            log_debug("method_call prepended object as first arg, type=%d", obj_type_id);
        }
    }
    else {
        if (is_method_call) {
            // Not a sys func method call - could be a user-defined method on the object
            // For now, fall back to building as a regular member expression call
            log_debug("method_call not resolved as sys func, building as regular call");
        }
        ast_node->function = build_expr(tp, function_node);
        if (ast_node->function->type->type_id == LMD_TYPE_FUNC) {
            TypeFunc* func_type = (TypeFunc*)ast_node->function->type;
            if (func_type->is_proc && !tp->current_scope->is_proc) {
                record_semantic_error(tp, call_node, ERR_PROC_IN_FN,
                    "procedure '%.*s' cannot be called in a function",
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
    // For method calls with sys func, arguments are appended after the object
    AstNode* prev_argument = is_method_call && sys_func_info ? method_object : NULL;
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
            // Handle string/symbol pattern definitions - wrap in TypeType for use in type expressions
            else if (entry->node->node_type == AST_NODE_STRING_PATTERN ||
                     entry->node->node_type == AST_NODE_SYMBOL_PATTERN) {
                TypeType* type_type = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
                type_type->type = entry->node->type;  // TypePattern*
                ast_node->type = (Type*)type_type;
                log_debug("Wrapped pattern definition %.*s in TypeType",
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
    // Empty strings ("" or '') map to null in Lambda
    String* str;
    log_debug("build lit string with symbol: %d", symbol);

    int cnt = ts_node_named_child_count(node);
    log_debug("string child count:: %d", cnt);
    
    // Handle empty string/symbol case - no content children means empty literal
    // When string is "" or symbol is '', cnt == 0 (no string_content/symbol_content children)
    // But for SYM_IDENT, cnt is also 0 (identifiers don't have named children)
    // Only return null for actual string/symbol literals that are empty
    if (cnt == 0 && (symbol == SYM_STRING || symbol == SYM_SYMBOL)) {
        log_debug("build_lit_string: empty string/symbol literal, returning null type");
        return &LIT_NULL;
    }
    
    // Calculate content length
    int len = 0;
    TSNode child;
    if (cnt == 0) {
        // For identifiers (SYM_IDENT), use the node itself
        child = node;
        int start = ts_node_start_byte(child), end = ts_node_end_byte(child);
        len = end - start;
    } else if (cnt == 1) {
        child = ts_node_named_child(node, 0);
        int start = ts_node_start_byte(child), end = ts_node_end_byte(child);
        len = end - start;
        // Check if content is empty after parsing (shouldn't happen normally)
        if (len == 0) {
            log_debug("build_lit_string: empty content child, returning null type");
            return &LIT_NULL;
        }
    }
    
    TypeString* str_type = (TypeString*)alloc_type(tp->pool,
        symbol == SYM_STRING ? LMD_TYPE_STRING :
        symbol == SYM_BINARY ? LMD_TYPE_BINARY :
        LMD_TYPE_SYMBOL, sizeof(TypeString));
    str_type->is_const = 1;  str_type->is_literal = 1;

    if (cnt <= 1) {
        // For cnt == 0 (identifiers) or cnt == 1 (simple string content)
        if (cnt == 0) {
            child = node;
        } else {
            child = ts_node_named_child(node, 0);
        }
        int start = ts_node_start_byte(child), end = ts_node_end_byte(child);
        len = end - start;
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
        
        // Check if the processed string is empty - return null type
        if (str->len == 0) {
            log_debug("build_lit_string: empty string after escape processing, returning null type");
            return &LIT_NULL;
        }
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
    // Use transpiler's decimal context via centralized function
    decimal->dec_val = decimal_parse_str(num_str, tp->decimal_ctx);
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
    else if (symbol == SYM_CURRENT_ITEM) {
        ast_node->expr = build_current_item(tp, child);
        ast_node->type = ast_node->expr->type;
    }
    else if (symbol == SYM_CURRENT_INDEX) {
        ast_node->expr = build_current_index(tp, child);
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
    else if (strview_equal(&op, "div")) { ast_node->op = OPERATOR_IDIV; }
    else if (strview_equal(&op, "%")) { ast_node->op = OPERATOR_MOD; }
    else if (strview_equal(&op, "==")) { ast_node->op = OPERATOR_EQ; }
    else if (strview_equal(&op, "!=")) { ast_node->op = OPERATOR_NE; }
    else if (strview_equal(&op, "<")) { ast_node->op = OPERATOR_LT; }
    else if (strview_equal(&op, "<=")) { ast_node->op = OPERATOR_LE; }
    else if (strview_equal(&op, ">")) { ast_node->op = OPERATOR_GT; }
    else if (strview_equal(&op, ">=")) { ast_node->op = OPERATOR_GE; }
    else if (strview_equal(&op, "to")) { ast_node->op = OPERATOR_TO; }
    else if (strview_equal(&op, "|")) { ast_node->op = OPERATOR_PIPE; }
    else if (strview_equal(&op, "where")) { ast_node->op = OPERATOR_WHERE; }
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
    else if (ast_node->op == OPERATOR_PIPE || ast_node->op == OPERATOR_WHERE) {
        // pipe operators: change node type to AST_NODE_PIPE
        ast_node->node_type = AST_NODE_PIPE;
        // determine result type based on operation
        if (ast_node->op == OPERATOR_WHERE) {
            // where preserves input collection type
            type_id = left_type;
        } else {
            // pipe: if left is collection, result is array; otherwise same as right
            if (left_type == LMD_TYPE_ARRAY || left_type == LMD_TYPE_LIST || 
                left_type == LMD_TYPE_RANGE || left_type == LMD_TYPE_MAP) {
                type_id = LMD_TYPE_ARRAY;
            } else {
                type_id = right_type;
            }
        }
    }
    else {  // OPERATOR_JOIN, etc.
        type_id = LMD_TYPE_ANY;
    }
    ast_node->type = alloc_type(tp->pool, type_id, sizeof(Type));
    log_debug("end build binary expr");
    return (AstNode*)ast_node;
}

// check if expression contains ~ or ~# references (pipe context references)
bool has_current_item_ref(AstNode* node) {
    if (!node) return false;
    
    switch (node->node_type) {
    case AST_NODE_CURRENT_ITEM:
    case AST_NODE_CURRENT_INDEX:
        return true;
    case AST_NODE_PRIMARY:
        return has_current_item_ref(((AstPrimaryNode*)node)->expr);
    case AST_NODE_UNARY:
        return has_current_item_ref(((AstUnaryNode*)node)->operand);
    case AST_NODE_BINARY:
    case AST_NODE_PIPE:
        return has_current_item_ref(((AstBinaryNode*)node)->left) ||
               has_current_item_ref(((AstBinaryNode*)node)->right);
    case AST_NODE_IF_EXPR:
    case AST_NODE_IF_STAM: {
        AstIfNode* if_node = (AstIfNode*)node;
        return has_current_item_ref(if_node->cond) ||
               has_current_item_ref(if_node->then) ||
               has_current_item_ref(if_node->otherwise);
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        if (has_current_item_ref(call->function)) return true;
        AstNode* arg = call->argument;
        while (arg) {
            if (has_current_item_ref(arg)) return true;
            arg = arg->next;
        }
        return false;
    }
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR:
        return has_current_item_ref(((AstFieldNode*)node)->object) ||
               has_current_item_ref(((AstFieldNode*)node)->field);
    case AST_NODE_ARRAY: {
        AstNode* item = ((AstArrayNode*)node)->item;
        while (item) {
            if (has_current_item_ref(item)) return true;
            item = item->next;
        }
        return false;
    }
    case AST_NODE_MAP: {
        AstNode* item = ((AstMapNode*)node)->item;
        while (item) {
            if (has_current_item_ref(item)) return true;
            item = item->next;
        }
        return false;
    }
    case AST_NODE_KEY_EXPR:
    case AST_NODE_ASSIGN:
        return has_current_item_ref(((AstNamedNode*)node)->as);
    default:
        return false;
    }
}

// build current item reference (~)
AstNode* build_current_item(Transpiler* tp, TSNode node) {
    log_debug("build current item (~)");
    AstNode* ast_node = alloc_ast_node(tp, AST_NODE_CURRENT_ITEM, node, sizeof(AstNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(Type));
    return ast_node;
}

// build current index reference (~#)
AstNode* build_current_index(Transpiler* tp, TSNode node) {
    log_debug("build current index (~#)");
    AstNode* ast_node = alloc_ast_node(tp, AST_NODE_CURRENT_INDEX, node, sizeof(AstNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(Type));
    return ast_node;
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

AstNode* build_decompose_expr(Transpiler* tp, TSNode asn_node, bool is_named);

AstNode* build_assign_expr(Transpiler* tp, TSNode asn_node, bool is_type_definition) {
    // Check if this is a decomposition (multiple names)
    // Count name fields to detect decomposition pattern
    TSTreeCursor cursor = ts_tree_cursor_new(asn_node);
    bool has_child = ts_tree_cursor_goto_first_child(&cursor);
    int name_count = 0;
    bool has_decompose_field = false;
    bool is_named_decompose = false;
    
    while (has_child) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_NAME) {
            name_count++;
        }
        if (field_id == FIELD_DECOMPOSE) {
            has_decompose_field = true;
            // Check if it's 'at' or '='
            TSNode decompose_node = ts_tree_cursor_current_node(&cursor);
            TSSymbol sym = ts_node_symbol(decompose_node);
            if (sym == anon_sym_at) {
                is_named_decompose = true;
            }
        }
        has_child = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    
    // If multiple names or explicit decompose field, use decomposition
    if (name_count > 1 || has_decompose_field) {
        return build_decompose_expr(tp, asn_node, is_named_decompose);
    }
    
    // Single variable assignment (existing logic)
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_ASSIGN, asn_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(asn_node, FIELD_NAME);
    int start_byte = ts_node_start_byte(name);
    StrView name_view = { .str = tp->source + start_byte, .length = ts_node_end_byte(name) - start_byte };
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);

    // check if the variable name is a reserved type keyword
    if (!is_type_definition && is_type_keyword(name_view)) {
        int line = ts_node_start_point(name).row + 1;
        record_type_error(tp, line, "Error: '%.*s' is a reserved type keyword and cannot be used as a variable name",
            (int)name_view.length, name_view.str);
    }

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
                // validate that type_expr is actually a type (TypeType)
                if (type_expr && type_expr->type && type_expr->type->type_id == LMD_TYPE_TYPE) {
                    ast_node->type = ((TypeType*)type_expr->type)->type;
                } else {
                    StrView type_str = ts_node_source(tp, type_node);
                    log_error("Error: invalid type annotation '%.*s' - not a valid type",
                        (int)type_str.length, type_str.str);
                    tp->error_count++;
                    ast_node->type = &TYPE_ANY;
                }
            }
        }
    }

    // push the name to the name stack
    push_name(tp, ast_node, NULL);
    return (AstNode*)ast_node;
}

// Build decomposition expression: let a, b = expr OR let a, b at expr
AstNode* build_decompose_expr(Transpiler* tp, TSNode asn_node, bool is_named) {
    log_debug("build decompose expr, is_named=%d", is_named);
    
    // Count names first
    TSTreeCursor cursor = ts_tree_cursor_new(asn_node);
    bool has_child = ts_tree_cursor_goto_first_child(&cursor);
    int name_count = 0;
    while (has_child) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_NAME) {
            name_count++;
        }
        has_child = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    
    if (name_count == 0) {
        log_error("decompose: no variable names found");
        return nullptr;
    }
    
    AstDecomposeNode* ast_node = (AstDecomposeNode*)alloc_ast_node(tp, AST_NODE_DECOMPOSE, asn_node, sizeof(AstDecomposeNode));
    ast_node->name_count = name_count;
    ast_node->is_named = is_named;
    ast_node->names = (String**)pool_calloc(tp->pool, sizeof(String*) * name_count);
    
    // Collect all names
    cursor = ts_tree_cursor_new(asn_node);
    has_child = ts_tree_cursor_goto_first_child(&cursor);
    int idx = 0;
    while (has_child) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_NAME) {
            TSNode name_node = ts_tree_cursor_current_node(&cursor);
            int start_byte = ts_node_start_byte(name_node);
            StrView name_view = { .str = tp->source + start_byte, .length = ts_node_end_byte(name_node) - start_byte };
            ast_node->names[idx] = name_pool_create_strview(tp->name_pool, name_view);
            log_debug("decompose name[%d]: %.*s", idx, (int)name_view.length, name_view.str);
            idx++;
        }
        has_child = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    
    // Build the source expression
    TSNode val_node = ts_node_child_by_field_id(asn_node, FIELD_AS);
    if (ts_node_is_null(val_node)) {
        log_error("decompose: missing source expression");
        ast_node->as = nullptr;
        ast_node->type = &TYPE_ANY;
    } else {
        ast_node->as = build_expr(tp, val_node);
        ast_node->type = &TYPE_ANY;  // decomposition itself doesn't have a single type
    }
    
    // Push all names to the name stack
    for (int i = 0; i < name_count; i++) {
        // Create a temporary named node for each variable
        AstNamedNode* var_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_ASSIGN, asn_node, sizeof(AstNamedNode));
        var_node->name = ast_node->names[i];
        var_node->type = &TYPE_ANY;  // type will be determined at runtime
        var_node->as = nullptr;
        push_name(tp, var_node, NULL);
    }
    
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
    log_debug("build type annotation");
    AstTypeNode* ast_node = (AstTypeNode*)alloc_ast_node(tp, AST_NODE_TYPE, type_node, sizeof(AstTypeNode));
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
            // validate that type_expr is actually a type (TypeType)
            if (type_expr && type_expr->type && type_expr->type->type_id == LMD_TYPE_TYPE) {
                fn_type->returned = ((TypeType*)type_expr->type)->type;
            } else {
                StrView type_str = ts_node_source(tp, child);
                log_error("Error: invalid return type '%.*s' - not a valid type",
                    (int)type_str.length, type_str.str);
                tp->error_count++;
                fn_type->returned = &TYPE_ANY;
            }
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
    TypeBinary* type = (TypeBinary*)alloc_type(tp->pool, LMD_TYPE_TYPE_BINARY, sizeof(TypeBinary));
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

// Helper function to parse occurrence count from string like "[2]", "[2, 5]", "[2+]"
// Sets min_count and max_count; max_count=-1 means unbounded
static void parse_occurrence_count(StrView op_str, int* min_count, int* max_count) {
    *min_count = 0;
    *max_count = -1;  // unbounded by default
    
    if (op_str.length < 3 || op_str.str[0] != '[') {
        return;
    }
    
    // skip opening bracket
    const char* p = op_str.str + 1;
    const char* end = op_str.str + op_str.length;
    
    // parse first number
    int n1 = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        n1 = n1 * 10 + (*p - '0');
        p++;
    }
    *min_count = n1;
    
    // skip whitespace
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    
    if (p < end) {
        if (*p == ']') {
            // [n] - exact count
            *max_count = n1;
        } else if (*p == '+') {
            // [n+] - unbounded minimum
            *max_count = -1;
        } else if (*p == ',') {
            // [n, m] - range
            p++;  // skip comma
            while (p < end && (*p == ' ' || *p == '\t')) p++;  // skip whitespace
            int n2 = 0;
            while (p < end && *p >= '0' && *p <= '9') {
                n2 = n2 * 10 + (*p - '0');
                p++;
            }
            *max_count = n2;
        }
    }
    
    log_debug("parsed occurrence: min=%d, max=%d from '%.*s'", *min_count, *max_count, (int)op_str.length, op_str.str);
}

AstNode* build_occurrence_type(Transpiler* tp, TSNode occurrence_node) {
    log_debug("build occurrence type");
    AstUnaryNode* ast_node = (AstUnaryNode*)alloc_ast_node(tp, AST_NODE_UNARY_TYPE, occurrence_node, sizeof(AstUnaryNode));
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeUnary* type = (TypeUnary*)alloc_type(tp->pool, LMD_TYPE_TYPE_UNARY, sizeof(TypeUnary));
    ((TypeType*)ast_node->type)->type = (Type*)type;
    
    // initialize occurrence counts to defaults
    type->min_count = 0;
    type->max_count = -1;

    TSNode op_node = ts_node_child_by_field_id(occurrence_node, FIELD_OPERATOR);
    TSSymbol op_symbol = ts_node_symbol(op_node);
    StrView op = ts_node_source(tp, op_node);
    ast_node->op_str = op;
    
    // The op_node is the 'occurrence' node which is a choice of '?', '+', '*', or occurrence_count
    // When it's occurrence_count, we need to check the first child or the source text
    // Check if op_symbol is 'occurrence' (the parent choice) and if it contains occurrence_count
    if (op_symbol == sym_occurrence) {
        // Get the first child which should be the actual choice
        TSNode actual_op = ts_node_child(op_node, 0);
        if (!ts_node_is_null(actual_op)) {
            op_symbol = ts_node_symbol(actual_op);
            log_debug("occurrence child symbol: %d (SYM_OCCURRENCE_COUNT=%d)", op_symbol, SYM_OCCURRENCE_COUNT);
        }
    }
    
    log_debug("build occurrence: op_symbol=%d, SYM_OCCURRENCE_COUNT=%d, op='%.*s'", 
              op_symbol, SYM_OCCURRENCE_COUNT, (int)op.length, op.str);
    
    if (op_symbol == SYM_OCCURRENCE_COUNT) {
        // Handle [n], [n, m], [n+] syntax
        ast_node->op = OPERATOR_REPEAT;
        parse_occurrence_count(op, &type->min_count, &type->max_count);
        log_debug("occurrence count: %.*s -> min=%d, max=%d", (int)op.length, op.str, type->min_count, type->max_count);
    } else if (strview_equal(&op, "?")) {
        ast_node->op = OPERATOR_OPTIONAL;
        type->min_count = 0;
        type->max_count = 1;
    } else if (strview_equal(&op, "+")) {
        ast_node->op = OPERATOR_ONE_MORE;
        type->min_count = 1;
        type->max_count = -1;
    } else if (strview_equal(&op, "*")) {
        ast_node->op = OPERATOR_ZERO_MORE;
        type->min_count = 0;
        type->max_count = -1;
    } else {
        log_debug("unknown operator: %.*s", (int)op.length, op.str);
    }
    
    type->op = ast_node->op;

    TSNode operand_node = ts_node_child_by_field_id(occurrence_node, FIELD_OPERAND);
    ast_node->operand = build_expr(tp, operand_node);
    type->operand = ast_node->operand->type;
    
    // Register the type in the type list for runtime access
    arraylist_append(tp->type_list, type);
    type->type_index = tp->type_list->length - 1;
    log_debug("occurrence type index: %d, op=%d, min=%d, max=%d", type->type_index, type->op, type->min_count, type->max_count);

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
    AstLoopNode* ast_node = (AstLoopNode*)alloc_ast_node(tp, AST_NODE_LOOP, loop_node, sizeof(AstLoopNode));
    ast_node->index_name = NULL;  // default: no index variable
    ast_node->is_named = false;   // default: 'in' keyword (not 'at')

    // Check for optional index variable (first identifier in 'for i, v in/at expr')
    TSNode index_node = ts_node_child_by_field_id(loop_node, FIELD_INDEX);
    if (!ts_node_is_null(index_node)) {
        int start_byte = ts_node_start_byte(index_node);
        StrView index_view = { .str = tp->source + start_byte, .length = ts_node_end_byte(index_node) - start_byte };
        ast_node->index_name = name_pool_create_strview(tp->name_pool, index_view);
        log_debug("loop has index variable: %.*s", (int)index_view.length, index_view.str);
    }

    // Get the main variable name
    TSNode name = ts_node_child_by_field_id(loop_node, FIELD_NAME);
    int start_byte = ts_node_start_byte(name);
    StrView name_view = { .str = tp->source + start_byte, .length = ts_node_end_byte(name) - start_byte };
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);

    // Check for 'at' keyword by examining the node children
    // The grammar produces different variants, we detect 'at' by checking if the source contains 'at'
    // A more robust way: check if the node contains 'at' token
    TSTreeCursor cursor = ts_tree_cursor_new(loop_node);
    bool has_child = ts_tree_cursor_goto_first_child(&cursor);
    while (has_child) {
        TSNode child = ts_tree_cursor_current_node(&cursor);
        TSSymbol sym = ts_node_symbol(child);
        if (sym == anon_sym_at) {
            ast_node->is_named = true;
            log_debug("loop uses 'at' keyword (named/attribute iteration)");
            break;
        }
        has_child = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);

    TSNode expr_node = ts_node_child_by_field_id(loop_node, FIELD_AS);
    ast_node->as = build_expr(tp, expr_node);

    // determine the type of the variable
    Type* expr_type = ast_node->as->type;
    if (ast_node->is_named) {
        // 'at' iteration: for elements, iterates attributes; for maps, iterates key-value pairs
        if (expr_type->type_id == LMD_TYPE_ELEMENT || expr_type->type_id == LMD_TYPE_MAP) {
            ast_node->type = &TYPE_ANY;  // attribute values can be any type
        } else {
            ast_node->type = &TYPE_ANY;
        }
    } else if (expr_type->type_id == LMD_TYPE_ARRAY || expr_type->type_id == LMD_TYPE_LIST) {
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

    // push the index name to the name stack if present
    if (ast_node->index_name) {
        // Create a temporary node for the index variable
        AstNamedNode* index_entry = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_LOOP, loop_node, sizeof(AstNamedNode));
        index_entry->name = ast_node->index_name;
        // For 'at' iteration, index (first var) is the key name (string)
        // For 'in' iteration, index (first var) is the numeric index (int)
        index_entry->type = ast_node->is_named ? &TYPE_STRING : &TYPE_INT;
        push_name(tp, index_entry, NULL);
    }

    // push the main name to the name stack
    push_name(tp, (AstNamedNode*)ast_node, NULL);
    return (AstNode*)ast_node;
}

// Helper: build order_spec node
AstNode* build_order_spec(Transpiler* tp, TSNode spec_node) {
    log_debug("build order spec");
    AstOrderSpec* ast_node = (AstOrderSpec*)alloc_ast_node(tp, AST_NODE_ORDER_SPEC, spec_node, sizeof(AstOrderSpec));
    
    TSNode expr_node = ts_node_child_by_field_id(spec_node, FIELD_EXPR);
    ast_node->expr = build_expr(tp, expr_node);
    
    // Check for direction (asc/desc)
    TSNode dir_node = ts_node_child_by_field_id(spec_node, FIELD_DIR);
    if (!ts_node_is_null(dir_node)) {
        StrView dir_text = ts_node_source(tp, dir_node);
        ast_node->descending = (strview_equal(&dir_text, "desc") || strview_equal(&dir_text, "descending"));
    } else {
        ast_node->descending = false;  // default ascending
    }
    
    ast_node->type = &TYPE_ANY;
    return (AstNode*)ast_node;
}

// Helper: build for_let_clause node (reuses AstNamedNode)
AstNode* build_for_let_clause(Transpiler* tp, TSNode let_node) {
    log_debug("build for let clause");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_ASSIGN, let_node, sizeof(AstNamedNode));
    
    TSNode name_node = ts_node_child_by_field_id(let_node, FIELD_NAME);
    if (ts_node_is_null(name_node)) {
        log_error("for_let_clause: name_node is null");
        return (AstNode*)ast_node;
    }
    StrView name_view = ts_node_source(tp, name_node);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);
    log_debug("for_let_clause: name = %.*s", (int)name_view.length, name_view.str);
    
    TSNode value_node = ts_node_child_by_field_id(let_node, FIELD_VALUE);
    if (ts_node_is_null(value_node)) {
        log_error("for_let_clause: value_node is null");
        ast_node->as = NULL;
        ast_node->type = &TYPE_ANY;
    } else {
        const char* value_type = ts_node_type(value_node);
        log_debug("for_let_clause: value_node type = %s", value_type);
        log_debug("for_let_clause: building value expression");
        ast_node->as = build_expr(tp, value_node);
        if (ast_node->as == NULL) {
            log_error("for_let_clause: build_expr returned NULL for type %s", value_type);
        }
        ast_node->type = ast_node->as ? ast_node->as->type : &TYPE_ANY;
    }
    
    // Register in current scope
    push_name(tp, ast_node, NULL);
    
    return (AstNode*)ast_node;
}

// Helper: build group by clause
AstNode* build_group_clause(Transpiler* tp, TSNode group_node) {
    log_debug("build group clause");
    AstGroupClause* ast_node = (AstGroupClause*)alloc_ast_node(tp, AST_NODE_GROUP_CLAUSE, group_node, sizeof(AstGroupClause));
    
    // Get the group name (from 'as name')
    TSNode name_node = ts_node_child_by_field_id(group_node, FIELD_NAME);
    StrView name_view = ts_node_source(tp, name_node);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);
    
    // Build key expressions (linked list)
    TSTreeCursor cursor = ts_tree_cursor_new(group_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode* prev_key = NULL;
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_KEY) {
            TSNode key_node = ts_tree_cursor_current_node(&cursor);
            AstNode* key_expr = build_expr(tp, key_node);
            if (prev_key == NULL) {
                ast_node->keys = key_expr;
            } else {
                prev_key->next = key_expr;
            }
            prev_key = key_expr;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    
    ast_node->type = &TYPE_ANY;
    return (AstNode*)ast_node;
}

// Helper function to build all for clauses (shared between for_expr and for_stam)
// Three-pass approach:
// Pass 1: Process loop declarations to register loop vars in scope
// Pass 2: Process let clauses (can reference loop vars)
// Pass 3: Process where, group, order, limit, offset (can reference both)
void build_for_clauses(Transpiler* tp, TSNode for_node, AstForNode* ast_node) {
    TSTreeCursor cursor = ts_tree_cursor_new(for_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode* prev_loop = NULL;
    AstNode* prev_let = NULL;
    AstNode* prev_order = NULL;
    
    // PASS 1: Process loop declarations to register loop vars in scope
    log_debug("for clauses pass 1: loop declarations");
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        TSNode child = ts_tree_cursor_current_node(&cursor);
        
        if (field_id == FIELD_DECLARE) {
            // Loop binding
            AstNode* loop = build_loop_expr(tp, child);
            log_debug("got loop type %d", loop->node_type);
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
    
    // PASS 2: Process let clauses (can reference loop vars)
    log_debug("for clauses pass 2: let clauses");
    cursor = ts_tree_cursor_new(for_node);
    has_node = ts_tree_cursor_goto_first_child(&cursor);
    
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        TSNode child = ts_tree_cursor_current_node(&cursor);
        
        if (field_id == FIELD_LET) {
            // Let clause - also registers name in scope
            AstNode* let_clause = build_for_let_clause(tp, child);
            if (prev_let == NULL) {
                ast_node->let_clause = let_clause;
            } else {
                prev_let->next = let_clause;
            }
            prev_let = let_clause;
        }
        
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    
    // PASS 3: Process clauses that reference variables (where, group, order, limit, offset)
    log_debug("for clauses pass 3: where/group/order/limit/offset");
    cursor = ts_tree_cursor_new(for_node);
    has_node = ts_tree_cursor_goto_first_child(&cursor);
    
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        TSNode child = ts_tree_cursor_current_node(&cursor);
        
        if (field_id == FIELD_WHERE) {
            // Where clause - get the condition expression
            TSNode cond_node = ts_node_child_by_field_id(child, FIELD_COND);
            ast_node->where = build_expr(tp, cond_node);
        }
        else if (field_id == FIELD_GROUP) {
            // Group by clause
            ast_node->group = (AstGroupClause*)build_group_clause(tp, child);
            // Register group name in scope (as a map with .key and .items)
            if (ast_node->group && ast_node->group->name) {
                AstNamedNode* group_var = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_ASSIGN, child, sizeof(AstNamedNode));
                group_var->name = ast_node->group->name;
                group_var->type = &TYPE_MAP;  // {key: ..., items: [...]}
                push_name(tp, group_var, NULL);
            }
        }
        else if (field_id == FIELD_ORDER) {
            // Order by clause - contains multiple order_spec
            TSTreeCursor order_cursor = ts_tree_cursor_new(child);
            bool has_spec = ts_tree_cursor_goto_first_child(&order_cursor);
            while (has_spec) {
                TSSymbol spec_field = ts_tree_cursor_current_field_id(&order_cursor);
                if (spec_field == FIELD_SPEC) {
                    TSNode spec_node = ts_tree_cursor_current_node(&order_cursor);
                    AstNode* order_spec = build_order_spec(tp, spec_node);
                    if (prev_order == NULL) {
                        ast_node->order = order_spec;
                    } else {
                        prev_order->next = order_spec;
                    }
                    prev_order = order_spec;
                }
                has_spec = ts_tree_cursor_goto_next_sibling(&order_cursor);
            }
            ts_tree_cursor_delete(&order_cursor);
        }
        else if (field_id == FIELD_LIMIT) {
            // Limit clause
            TSNode count_node = ts_node_child_by_field_id(child, FIELD_COUNT);
            ast_node->limit = build_expr(tp, count_node);
        }
        else if (field_id == FIELD_OFFSET) {
            // Offset clause
            TSNode count_node = ts_node_child_by_field_id(child, FIELD_COUNT);
            ast_node->offset = build_expr(tp, count_node);
        }
        
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    
    if (!ast_node->loop) {
        log_error("Error: missing for loop declare");
    }
}

AstNode* build_for_expr(Transpiler* tp, TSNode for_node) {
    log_debug("build for expr");
    AstForNode* ast_node = (AstForNode*)alloc_ast_node(tp, AST_NODE_FOR_EXPR, for_node, sizeof(AstForNode));
    // Type will be determined after processing the 'then' expression

    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    tp->current_scope = ast_node->vars;
    
    // Build all clauses (loop, let, where, group, order, limit, offset)
    build_for_clauses(tp, for_node, ast_node);

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
    ast_node->vars->is_proc = tp->current_scope->is_proc;  // inherit proc context
    tp->current_scope = ast_node->vars;

    // Build all clauses (loop, let, where, group, order, limit, offset)
    build_for_clauses(tp, for_node, ast_node);

    TSNode then_node = ts_node_child_by_field_id(for_node, FIELD_THEN);
    ast_node->then = build_expr(tp, then_node);
    log_debug("got for then type %d", ast_node->then->node_type);

    // for statement returns type
    ast_node->type = &TYPE_ANY;
    tp->current_scope = ast_node->vars->parent;
    return (AstNode*)ast_node;
}

// while statement (procedural only)
AstNode* build_while_stam(Transpiler* tp, TSNode while_node) {
    log_debug("build while stam");
    
    // Check if we're in a procedural context
    if (!tp->current_scope->is_proc) {
        log_error("Error: 'while' statement is only allowed in procedural functions (pn)");
        return NULL;
    }
    
    AstWhileNode* ast_node = (AstWhileNode*)alloc_ast_node(tp, AST_NODE_WHILE_STAM, while_node, sizeof(AstWhileNode));

    ast_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    ast_node->vars->parent = tp->current_scope;
    ast_node->vars->is_proc = true;
    tp->current_scope = ast_node->vars;

    // build condition
    TSNode cond_node = ts_node_child_by_field_id(while_node, FIELD_COND);
    ast_node->cond = build_expr(tp, cond_node);
    log_debug("got while cond type %d", ast_node->cond ? ast_node->cond->node_type : -1);

    // build body
    TSNode body_node = ts_node_child_by_field_id(while_node, FIELD_BODY);
    ast_node->body = build_expr(tp, body_node);
    log_debug("got while body type %d", ast_node->body ? ast_node->body->node_type : -1);

    ast_node->type = &TYPE_ANY;
    tp->current_scope = ast_node->vars->parent;
    return (AstNode*)ast_node;
}

// break statement (procedural only)
AstNode* build_break_stam(Transpiler* tp, TSNode break_node) {
    log_debug("build break stam");
    
    // Check if we're in a procedural context
    if (!tp->current_scope->is_proc) {
        log_error("Error: 'break' statement is only allowed in procedural functions (pn)");
        return NULL;
    }
    
    AstNode* ast_node = alloc_ast_node(tp, AST_NODE_BREAK_STAM, break_node, sizeof(AstNode));
    ast_node->type = &TYPE_ANY;
    return ast_node;
}

// continue statement (procedural only)
AstNode* build_continue_stam(Transpiler* tp, TSNode continue_node) {
    log_debug("build continue stam");
    
    // Check if we're in a procedural context
    if (!tp->current_scope->is_proc) {
        log_error("Error: 'continue' statement is only allowed in procedural functions (pn)");
        return NULL;
    }
    
    AstNode* ast_node = alloc_ast_node(tp, AST_NODE_CONTINUE_STAM, continue_node, sizeof(AstNode));
    ast_node->type = &TYPE_ANY;
    return ast_node;
}

// return statement (procedural only)
AstNode* build_return_stam(Transpiler* tp, TSNode return_node) {
    log_debug("build return stam");
    
    // Check if we're in a procedural context
    if (!tp->current_scope->is_proc) {
        log_error("Error: 'return' statement is only allowed in procedural functions (pn)");
        return NULL;
    }
    
    AstReturnNode* ast_node = (AstReturnNode*)alloc_ast_node(tp, AST_NODE_RETURN_STAM, return_node, sizeof(AstReturnNode));
    
    // build optional return value
    TSNode value_node = ts_node_child_by_field_id(return_node, FIELD_VALUE);
    if (!ts_node_is_null(value_node)) {
        ast_node->value = build_expr(tp, value_node);
        ast_node->type = ast_node->value ? ast_node->value->type : &TYPE_ANY;
    } else {
        ast_node->value = NULL;
        ast_node->type = &TYPE_NULL;
    }
    
    return (AstNode*)ast_node;
}

// var statement for mutable variables (procedural only)
AstNode* build_var_stam(Transpiler* tp, TSNode var_node) {
    log_debug("build var stam");
    
    // Check if we're in a procedural context
    if (!tp->current_scope->is_proc) {
        log_error("Error: 'var' statement is only allowed in procedural functions (pn)");
        return NULL;
    }
    
    // Reuse the let statement builder but mark as VAR_STAM
    AstLetNode* ast_node = (AstLetNode*)alloc_ast_node(tp, AST_NODE_VAR_STAM, var_node, sizeof(AstLetNode));
    
    // Build declarations (same logic as build_let_and_type_stam for let)
    TSTreeCursor cursor = ts_tree_cursor_new(var_node);
    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
    AstNode* prev_declare = NULL;
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode* assign = build_assign_expr(tp, child, false);
            if (prev_declare == NULL) {
                ast_node->declare = assign;
            } else {
                prev_declare->next = assign;
            }
            prev_declare = assign;
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    
    ast_node->type = &TYPE_ANY;
    return (AstNode*)ast_node;
}

// assignment statement for mutable variables (procedural only)
AstNode* build_assign_stam(Transpiler* tp, TSNode assign_node) {
    log_debug("build assign stam");
    
    // Check if we're in a procedural context
    if (!tp->current_scope->is_proc) {
        log_error("Error: assignment statement is only allowed in procedural functions (pn)");
        return NULL;
    }
    
    AstAssignStamNode* ast_node = (AstAssignStamNode*)alloc_ast_node(tp, AST_NODE_ASSIGN_STAM, assign_node, sizeof(AstAssignStamNode));
    
    // get target identifier
    TSNode target_node = ts_node_child_by_field_id(assign_node, FIELD_TARGET);
    StrView target_str = ts_node_source(tp, target_node);
    ast_node->target = name_pool_create_strview(tp->name_pool, target_str);
    
    // build value expression
    TSNode value_node = ts_node_child_by_field_id(assign_node, FIELD_VALUE);
    ast_node->value = build_expr(tp, value_node);
    ast_node->type = ast_node->value ? ast_node->value->type : &TYPE_ANY;
    
    // TODO: Verify target is a mutable variable (var, not let)
    
    return (AstNode*)ast_node;
}

// returns NULL for variadic marker (...)
AstNamedNode* build_param_expr(Transpiler* tp, TSNode param_node, bool is_type) {
    log_debug("build param expr");

    // check for variadic marker (...)
    TSNode variadic = ts_node_child_by_field_id(param_node, FIELD_VARIADIC);
    if (!ts_node_is_null(variadic)) {
        log_debug("build param: variadic marker found");
        return NULL;  // special marker, will be handled by build_func
    }

    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_PARAM, param_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(param_node, FIELD_NAME);
    StrView name_str = ts_node_source(tp, name);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_str);

    // allocate TypeParam for this parameter
    TypeParam* param_type = (TypeParam*)alloc_type(tp->pool, LMD_TYPE_ANY, sizeof(TypeParam));
    ast_node->type = (Type*)param_type;

    // check optional marker (?)
    TSNode optional_node = ts_node_child_by_field_id(param_node, FIELD_OPTIONAL);
    param_type->is_optional = !ts_node_is_null(optional_node);

    // check for default value expression
    TSNode default_node = ts_node_child_by_field_id(param_node, FIELD_DEFAULT);
    if (!ts_node_is_null(default_node)) {
        param_type->default_value = build_expr(tp, default_node);
        param_type->is_optional = true;  // param with default is implicitly optional
    }

    // determine the type of the parameter
    TSNode type_node = ts_node_child_by_field_id(param_node, FIELD_TYPE);
    if (!ts_node_is_null(type_node)) {
        AstNode* type_expr = build_expr(tp, type_node);
        // validate that type_expr is actually a type (TypeType)
        if (type_expr && type_expr->type && type_expr->type->type_id == LMD_TYPE_TYPE) {
            TypeType* type_type = (TypeType*)type_expr->type;
            if (type_type->type) {
                *(Type*)param_type = *type_type->type;
            }
        } else {
            // invalid type annotation - log error but continue with ANY type
            StrView type_str = ts_node_source(tp, type_node);
            log_error("Error: invalid type annotation '%.*s' - not a valid type",
                (int)type_str.length, type_str.str);
            tp->error_count++;
        }
    }
    else {
        *(Type*)param_type = ast_node->as ? *ast_node->as->type : TYPE_ANY;
    }

    if (!is_type) { push_name(tp, ast_node, NULL); }
    return ast_node;
}

// build named argument in function call: name: value
AstNode* build_named_argument(Transpiler* tp, TSNode arg_node) {
    log_debug("build named argument");
    AstNamedNode* ast_node = (AstNamedNode*)alloc_ast_node(tp, AST_NODE_NAMED_ARG, arg_node, sizeof(AstNamedNode));

    TSNode name = ts_node_child_by_field_id(arg_node, FIELD_NAME);
    StrView name_str = ts_node_source(tp, name);
    ast_node->name = name_pool_create_strview(tp->name_pool, name_str);

    TSNode value_node = ts_node_child_by_field_id(arg_node, FIELD_VALUE);
    ast_node->as = build_expr(tp, value_node);
    ast_node->type = ast_node->as ? ast_node->as->type : &TYPE_ANY;

    log_debug("named argument: %s", ast_node->name);
    return (AstNode*)ast_node;
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
    AstNamedNode* prev_param = NULL;  int param_count = 0;  int required_count = 0;
    bool seen_optional = false;  // track if we've seen an optional param
    while (has_node) {
        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
        if (field_id == FIELD_DECLARE) {  // param declaration
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNamedNode* param = build_param_expr(tp, child, false);

            // check for variadic marker (NULL return from build_param_expr)
            if (param == NULL) {
                fn_type->is_variadic = true;
                log_debug("function is variadic");
                has_node = ts_tree_cursor_goto_next_sibling(&cursor);
                continue;
            }

            TypeParam* param_type = (TypeParam*)param->type;
            log_debug("got param: %s, optional=%d", param->name, param_type->is_optional);

            // validate param ordering: required params must come before optional
            if (param_type->is_optional) {
                seen_optional = true;
            } else {
                if (seen_optional) {
                    log_error("required parameter '%s' cannot follow optional parameter", param->name);
                }
                required_count++;
            }

            if (prev_param == NULL) {
                ast_node->param = param;
                fn_type->param = param_type;
            }
            else {
                prev_param->next = (AstNode*)param;
                ((TypeParam*)prev_param->type)->next = param_type;
            }
            prev_param = param;  param_count++;
        }
        else if (field_id == FIELD_TYPE) {  // return type
            TSNode child = ts_tree_cursor_current_node(&cursor);
            AstNode* type_expr = build_expr(tp, child);
            // validate that type_expr is actually a type (TypeType)
            if (type_expr && type_expr->type && type_expr->type->type_id == LMD_TYPE_TYPE) {
                fn_type->returned = ((TypeType*)type_expr->type)->type;
            } else {
                StrView type_str = ts_node_source(tp, child);
                log_error("Error: invalid return type '%.*s' - not a valid type",
                    (int)type_str.length, type_str.str);
                tp->error_count++;
                fn_type->returned = &TYPE_ANY;
            }
        }
        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
    }
    ts_tree_cursor_delete(&cursor);
    fn_type->param_count = param_count;
    fn_type->required_param_count = required_count;

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
    
    // Analyze captures for closure support
    NameScope* global_scope = find_global_scope(ast_node->vars);
    analyze_captures(tp, ast_node, global_scope);
    
    log_debug("end building fn");
    return (AstNode*)ast_node;
}

AstNode* build_content(Transpiler* tp, TSNode list_node, bool flattern, bool is_global) {
    log_debug("build content, is_global=%d", is_global);
    AstListNode* ast_node = (AstListNode*)alloc_ast_node(tp, AST_NODE_CONTENT, list_node, sizeof(AstListNode));
    TypeList* type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_LIST, sizeof(TypeList));
    ast_node->list_type = type;
    ast_node->type = &TYPE_ANY;  // content() returns Item, not List

    // Two-pass compilation for top-level functions (only when is_global is true)
    if (is_global) {
        log_debug("pass 1: scanning for top-level function declarations");
        // Pass 1: Scan and register all top-level function names with placeholder nodes
        TSNode child = ts_node_named_child(list_node, 0);
        while (!ts_node_is_null(child)) {
            TSSymbol symbol = ts_node_symbol(child);
            if (symbol == SYM_FUNC_STAM || symbol == SYM_FUNC_EXPR_STAM) {
                // Create minimal placeholder function node
                bool is_proc = false;
                TSNode kind = ts_node_child_by_field_id(child, FIELD_KIND);
                if (!ts_node_is_null(kind)) {
                    StrView kind_str = ts_node_source(tp, kind);
                    is_proc = strview_equal(&kind_str, "pn");
                }

                AstFuncNode* fn_node = (AstFuncNode*)alloc_ast_node(tp,
                    is_proc ? AST_NODE_PROC : AST_NODE_FUNC, child, sizeof(AstFuncNode));
                fn_node->type = alloc_type(tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
                TypeFunc* fn_type = (TypeFunc*)fn_node->type;
                fn_type->is_anonymous = false;
                fn_type->is_proc = is_proc;
                fn_type->returned = &TYPE_ANY;  // Safe default for forward references
                
                // Initialize required fields to prevent crashes
                fn_node->param = NULL;
                fn_node->body = NULL;
                fn_node->vars = NULL;
                fn_node->captures = NULL;

                // Get function name and register it early
                TSNode fn_name_node = ts_node_child_by_field_id(child, FIELD_NAME);
                StrView name = ts_node_source(tp, fn_name_node);
                fn_node->name = name_pool_create_strview(tp->name_pool, name);
                
                log_debug("pass 1: registering function placeholder '%.*s'", (int)fn_node->name->len, fn_node->name->chars);
                push_name(tp, (AstNamedNode*)fn_node, NULL);
            }
            child = ts_node_next_named_sibling(child);
        }
    }

    // Pass 2: build all content (functions and other expressions)
    log_debug("pass 2: building content bodies");
    TSNode child = ts_node_named_child(list_node, 0);
    AstNode* prev_item = NULL;
    while (!ts_node_is_null(child)) {
        TSSymbol symbol = ts_node_symbol(child);
        AstNode* item = NULL;
        
        if (symbol == SYM_FUNC_STAM || symbol == SYM_FUNC_EXPR_STAM) {
            if (is_global) {
                // For global functions, look up the pre-registered placeholder
                TSNode fn_name_node = ts_node_child_by_field_id(child, FIELD_NAME);
                StrView name = ts_node_source(tp, fn_name_node);
                NameEntry* entry = lookup_name(tp, name);
                
                if (entry && entry->node && 
                    (entry->node->node_type == AST_NODE_FUNC || entry->node->node_type == AST_NODE_PROC)) {
                    AstFuncNode* fn_node = (AstFuncNode*)entry->node;
                    log_debug("pass 2: completing function '%.*s'", (int)fn_node->name->len, fn_node->name->chars);
                    
                    // Now build the complete function body
                    TypeFunc* fn_type = (TypeFunc*)fn_node->type;
                    
                    // 'pub' flag
                    TSNode pub = ts_node_child_by_field_id(child, FIELD_PUB);
                    fn_type->is_public = !ts_node_is_null(pub);
                    
                    // Build parameters
                    fn_node->vars = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
                    fn_node->vars->parent = tp->current_scope;
                    fn_node->vars->is_proc = fn_type->is_proc;
                    tp->current_scope = fn_node->vars;
                    
                    TSTreeCursor cursor = ts_tree_cursor_new(child);
                    bool has_node = ts_tree_cursor_goto_first_child(&cursor);
                    AstNamedNode* prev_param = NULL;
                    int param_count = 0;
                    int required_count = 0;
                    bool seen_optional = false;
                    bool has_declared_return_type = false;
                    
                    while (has_node) {
                        TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
                        if (field_id == FIELD_DECLARE) {
                            TSNode param_child = ts_tree_cursor_current_node(&cursor);
                            AstNamedNode* param = build_param_expr(tp, param_child, false);
                            
                            if (param == NULL) {
                                fn_type->is_variadic = true;
                                log_debug("function is variadic");
                                has_node = ts_tree_cursor_goto_next_sibling(&cursor);
                                continue;
                            }
                            
                            TypeParam* param_type = (TypeParam*)param->type;
                            log_debug("got param: %.*s, optional=%d", (int)param->name->len, param->name->chars, param_type->is_optional);
                            
                            if (param_type->is_optional) {
                                seen_optional = true;
                            } else {
                                if (seen_optional) {
                                    log_error("required parameter '%.*s' cannot follow optional parameter", (int)param->name->len);
                                }
                                required_count++;
                            }
                            
                            if (prev_param == NULL) {
                                fn_node->param = param;
                                fn_type->param = param_type;
                            } else {
                                prev_param->next = (AstNode*)param;
                                ((TypeParam*)prev_param->type)->next = param_type;
                            }
                            prev_param = param;
                            param_count++;
                        }
                        else if (field_id == FIELD_TYPE) {
                            TSNode type_child = ts_tree_cursor_current_node(&cursor);
                            AstNode* type_expr = build_expr(tp, type_child);
                            if (type_expr && type_expr->type && type_expr->type->type_id == LMD_TYPE_TYPE) {
                                fn_type->returned = ((TypeType*)type_expr->type)->type;
                                has_declared_return_type = true;
                            } else {
                                StrView type_str = ts_node_source(tp, type_child);
                                log_error("Error: invalid return type '%.*s' - not a valid type",
                                    (int)type_str.length, type_str.str);
                                tp->error_count++;
                                fn_type->returned = &TYPE_ANY;
                            }
                        }
                        has_node = ts_tree_cursor_goto_next_sibling(&cursor);
                    }
                    ts_tree_cursor_delete(&cursor);
                    
                    fn_type->param_count = param_count;
                    fn_type->required_param_count = required_count;
                    
                    // Build function body
                    TSNode fn_body_node = ts_node_child_by_field_id(child, FIELD_BODY);
                    fn_node->body = build_expr(tp, fn_body_node);
                    
                    // If no return type was declared, keep it as &TYPE_ANY (Item)
                    // This ensures forward-referenced functions work correctly
                    if (!has_declared_return_type) {
                        fn_type->returned = &TYPE_ANY;  // Safe for all forward refs
                    }
                    
                    // Restore parent scope
                    tp->current_scope = fn_node->vars->parent;
                    
                    // Analyze captures
                    NameScope* global_scope = find_global_scope(fn_node->vars->parent);
                    analyze_captures(tp, fn_node, global_scope);
                    
                    item = (AstNode*)fn_node;
                    log_debug("pass 2: completed function '%.*s' with body=%p", 
                        (int)fn_node->name->len, fn_node->name->chars, fn_node->body);
                } else {
                    log_error("Error: failed to find pre-registered function for '%.*s'", 
                        (int)name.length, name.str);
                    // Fallback: build normally
                    item = build_func(tp, child, true, is_global);
                }
            } else {
                // For non-global functions, use original single-pass behavior
                item = build_func(tp, child, true, is_global);
            }
        } else {
            item = build_expr(tp, child);
        }
        
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
    case SYM_BINARY_EXPR_NO_PIPE:
        return build_binary_expr(tp, expr_node);
    case SYM_CURRENT_ITEM:
        return build_current_item(tp, expr_node);
    case SYM_CURRENT_INDEX:
        return build_current_index(tp, expr_node);
    case SYM_LET_EXPR:
        return build_let_expr(tp, expr_node);
    case SYM_LET_STAM:  case SYM_PUB_STAM:  case SYM_TYPE_DEFINE:
        return build_let_and_type_stam(tp, expr_node, symbol);
    case SYM_FOR_EXPR:
        return build_for_expr(tp, expr_node);
    case SYM_FOR_STAM:
        return build_for_stam(tp, expr_node);
    case SYM_WHILE_STAM:
        return build_while_stam(tp, expr_node);
    case SYM_BREAK_STAM:
        return build_break_stam(tp, expr_node);
    case SYM_CONTINUE_STAM:
        return build_continue_stam(tp, expr_node);
    case SYM_RETURN_STAM:
        return build_return_stam(tp, expr_node);
    case SYM_VAR_STAM:
        return build_var_stam(tp, expr_node);
    case SYM_ASSIGN_STAM:
        return build_assign_stam(tp, expr_node);
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
    case SYM_NAMED_ARGUMENT:
        return build_named_argument(tp, expr_node);
    case SYM_IMPORT_MODULE:
        // already processed
        return NULL;
    case SYM_COMMENT:
        return NULL;
    // String/Symbol Pattern nodes
    case SYM_STRING_PATTERN:
        return build_string_pattern(tp, expr_node, false);
    case SYM_SYMBOL_PATTERN:
        return build_string_pattern(tp, expr_node, true);
    case SYM_INDEX:
        // This is likely a parsing error - index tokens should not appear as standalone expressions
        // Common cause: malformed syntax like "1..3" which parses as "1." + ".3"
        // or case like "12.34.56"
        record_semantic_error(tp, expr_node, ERR_INVALID_LITERAL,
            "Unexpected token - possible malformed number or range (use 'to' instead of '..')");
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

// ==================== String/Symbol Pattern Building ====================

// Build pattern character class (\d, \w, \s, \a, .)
AstNode* build_pattern_char_class(Transpiler* tp, TSNode node) {
    log_debug("build pattern char class");
    AstPatternCharClassNode* ast_node = (AstPatternCharClassNode*)
        alloc_ast_node(tp, AST_NODE_PATTERN_CHAR_CLASS, node, sizeof(AstPatternCharClassNode));
    
    StrView source = ts_node_source(tp, node);
    
    if (source.length == 2 && source.str[0] == '\\') {
        switch (source.str[1]) {
        case 'd': ast_node->char_class = PATTERN_DIGIT; break;
        case 'w': ast_node->char_class = PATTERN_WORD; break;
        case 's': ast_node->char_class = PATTERN_SPACE; break;
        case 'a': ast_node->char_class = PATTERN_ALPHA; break;
        default: ast_node->char_class = PATTERN_ANY; break;
        }
    } else {
        // Single dot for any character
        ast_node->char_class = PATTERN_ANY;
    }
    
    // Type is pattern
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
    return (AstNode*)ast_node;
}

// Build pattern range node ("a" to "z")
AstNode* build_pattern_range(Transpiler* tp, TSNode node) {
    log_debug("build pattern range");
    AstPatternRangeNode* ast_node = (AstPatternRangeNode*)
        alloc_ast_node(tp, AST_NODE_PATTERN_RANGE, node, sizeof(AstPatternRangeNode));
    
    TSNode left_node = ts_node_child_by_field_id(node, FIELD_LEFT);
    TSNode right_node = ts_node_child_by_field_id(node, FIELD_RIGHT);
    
    ast_node->start = build_lit_node(tp, left_node, true, SYM_STRING);
    ast_node->end = build_lit_node(tp, right_node, true, SYM_STRING);
    
    // Type is pattern
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
    return (AstNode*)ast_node;
}

// Build primary pattern expression
AstNode* build_primary_pattern(Transpiler* tp, TSNode node) {
    log_debug("build primary pattern");
    uint32_t child_count = ts_node_child_count(node);
    
    // Check for parenthesized expression: ( _pattern_expr )
    if (child_count == 3) {
        TSNode first = ts_node_child(node, 0);
        StrView first_src = ts_node_source(tp, first);
        if (first_src.length == 1 && first_src.str[0] == '(') {
            return build_pattern_expr(tp, ts_node_child(node, 1));
        }
    }
    
    // Single child - delegate to appropriate builder
    TSNode child = ts_node_child(node, 0);
    TSSymbol symbol = ts_node_symbol(child);
    
    if (symbol == SYM_STRING) {
        return build_lit_node(tp, child, true, SYM_STRING);
    } else if (symbol == SYM_PATTERN_CHAR_CLASS) {
        return build_pattern_char_class(tp, child);
    } else if (symbol == SYM_PATTERN_ANY) {
        // Build a char class node for '\.'
        AstPatternCharClassNode* ast_node = (AstPatternCharClassNode*)
            alloc_ast_node(tp, AST_NODE_PATTERN_CHAR_CLASS, child, sizeof(AstPatternCharClassNode));
        ast_node->char_class = PATTERN_ANY;
        ast_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
        return (AstNode*)ast_node;
    } else if (symbol == SYM_PATTERN_ANY_STAR) {
        // Build a unary node for '...' (equivalent to \.*)
        AstUnaryNode* ast_node = (AstUnaryNode*)
            alloc_ast_node(tp, AST_NODE_UNARY, child, sizeof(AstUnaryNode));
        // Create the operand (PATTERN_ANY)
        AstPatternCharClassNode* any_node = (AstPatternCharClassNode*)
            alloc_ast_node(tp, AST_NODE_PATTERN_CHAR_CLASS, child, sizeof(AstPatternCharClassNode));
        any_node->char_class = PATTERN_ANY;
        any_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
        ast_node->operand = (AstNode*)any_node;
        ast_node->op = OPERATOR_ZERO_MORE;
        ast_node->op_str = {.str = "*", .length = 1};
        ast_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
        return (AstNode*)ast_node;
    }
    
    log_error("build_primary_pattern: unknown child symbol %d", symbol);
    return nullptr;
}

// Build pattern occurrence (pattern with ?, +, *, [n], [n+], [n, m])
AstNode* build_pattern_occurrence(Transpiler* tp, TSNode node) {
    log_debug("build pattern occurrence");
    AstUnaryNode* ast_node = (AstUnaryNode*)alloc_ast_node(tp, AST_NODE_UNARY, node, sizeof(AstUnaryNode));
    
    TSNode operand_node = ts_node_child_by_field_id(node, FIELD_OPERAND);
    TSNode operator_node = ts_node_child_by_field_id(node, FIELD_OPERATOR);
    TSSymbol op_symbol = ts_node_symbol(operator_node);
    
    ast_node->operand = build_pattern_expr(tp, operand_node);
    ast_node->op_str = ts_node_source(tp, operator_node);
    
    // Determine operator from string or symbol
    if (op_symbol == SYM_PATTERN_COUNT) {
        // [n], [n+], [n, m] - treat as OPERATOR_REPEAT
        ast_node->op = OPERATOR_REPEAT;
        log_debug("pattern count: %.*s", (int)ast_node->op_str.length, ast_node->op_str.str);
    } else if (ast_node->op_str.length == 1) {
        switch (ast_node->op_str.str[0]) {
        case '?': ast_node->op = OPERATOR_OPTIONAL; break;
        case '+': ast_node->op = OPERATOR_ONE_MORE; break;
        case '*': ast_node->op = OPERATOR_ZERO_MORE; break;
        default: ast_node->op = OPERATOR_REPEAT; break;
        }
    } else {
        // Fallback to OPERATOR_REPEAT for any other format
        ast_node->op = OPERATOR_REPEAT;
    }
    
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
    return (AstNode*)ast_node;
}

// Build pattern negation (!pattern)
AstNode* build_pattern_negation(Transpiler* tp, TSNode node) {
    log_debug("build pattern negation");
    AstUnaryNode* ast_node = (AstUnaryNode*)alloc_ast_node(tp, AST_NODE_UNARY, node, sizeof(AstUnaryNode));
    
    TSNode operand_node = ts_node_child_by_field_id(node, FIELD_OPERAND);
    ast_node->operand = build_pattern_expr(tp, operand_node);
    ast_node->op = OPERATOR_NOT;
    ast_node->op_str = {.str = "!", .length = 1};
    
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
    return (AstNode*)ast_node;
}

// Build binary pattern expression (| or &)
AstNode* build_binary_pattern(Transpiler* tp, TSNode node) {
    log_debug("build binary pattern");
    AstBinaryNode* ast_node = (AstBinaryNode*)alloc_ast_node(tp, AST_NODE_BINARY, node, sizeof(AstBinaryNode));
    
    TSNode left_node = ts_node_child_by_field_id(node, FIELD_LEFT);
    TSNode op_node = ts_node_child_by_field_id(node, FIELD_OPERATOR);
    TSNode right_node = ts_node_child_by_field_id(node, FIELD_RIGHT);
    
    ast_node->left = build_pattern_expr(tp, left_node);
    ast_node->right = build_pattern_expr(tp, right_node);
    ast_node->op_str = ts_node_source(tp, op_node);
    
    // Determine operator
    if (ast_node->op_str.length == 1) {
        switch (ast_node->op_str.str[0]) {
        case '|': ast_node->op = OPERATOR_UNION; break;
        case '&': ast_node->op = OPERATOR_INTERSECT; break;
        default: ast_node->op = OPERATOR_UNION; break;
        }
    }
    
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
    return (AstNode*)ast_node;
}

// Main pattern expression builder - dispatches to specific builders
AstNode* build_pattern_expr(Transpiler* tp, TSNode node) {
    TSSymbol symbol = ts_node_symbol(node);
    log_debug("build_pattern_expr: symbol=%d (%s)", symbol, ts_node_type(node));
    
    if (symbol == SYM_STRING) {
        return build_lit_node(tp, node, true, SYM_STRING);
    }
    else if (symbol == SYM_PATTERN_CHAR_CLASS) {
        return build_pattern_char_class(tp, node);
    }
    else if (symbol == SYM_PATTERN_ANY) {
        AstPatternCharClassNode* ast_node = (AstPatternCharClassNode*)
            alloc_ast_node(tp, AST_NODE_PATTERN_CHAR_CLASS, node, sizeof(AstPatternCharClassNode));
        ast_node->char_class = PATTERN_ANY;
        ast_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
        return (AstNode*)ast_node;
    }
    else if (symbol == SYM_PATTERN_ANY_STAR) {
        // Build a unary node for '...' (equivalent to \.*)
        AstUnaryNode* ast_node = (AstUnaryNode*)
            alloc_ast_node(tp, AST_NODE_UNARY, node, sizeof(AstUnaryNode));
        AstPatternCharClassNode* any_node = (AstPatternCharClassNode*)
            alloc_ast_node(tp, AST_NODE_PATTERN_CHAR_CLASS, node, sizeof(AstPatternCharClassNode));
        any_node->char_class = PATTERN_ANY;
        any_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
        ast_node->operand = (AstNode*)any_node;
        ast_node->op = OPERATOR_ZERO_MORE;
        ast_node->op_str = {.str = "*", .length = 1};
        ast_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
        return (AstNode*)ast_node;
    }
    else if (symbol == SYM_PATTERN_RANGE) {
        return build_pattern_range(tp, node);
    }
    else if (symbol == SYM_PATTERN_OCCURRENCE) {
        return build_pattern_occurrence(tp, node);
    }
    else if (symbol == SYM_PATTERN_NEGATION) {
        return build_pattern_negation(tp, node);
    }
    else if (symbol == SYM_BINARY_PATTERN) {
        return build_binary_pattern(tp, node);
    }
    else if (symbol == SYM_PRIMARY_PATTERN) {
        return build_primary_pattern(tp, node);
    }
    else if (symbol == SYM_PATTERN_SEQ) {
        // Pattern sequence - concatenation of multiple patterns
        AstPatternSeqNode* seq_node = (AstPatternSeqNode*)
            alloc_ast_node(tp, AST_NODE_PATTERN_SEQ, node, sizeof(AstPatternSeqNode));
        seq_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
        
        uint32_t child_count = ts_node_named_child_count(node);
        log_debug("build_pattern_seq: %d children", child_count);
        
        AstNode* prev = nullptr;
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_named_child(node, i);
            AstNode* child_node = build_pattern_expr(tp, child);
            if (child_node) {
                if (prev) {
                    prev->next = child_node;
                } else {
                    seq_node->first = child_node;
                }
                prev = child_node;
            }
        }
        return (AstNode*)seq_node;
    }
    else if (symbol == SYM_IDENT) {
        // Pattern reference by identifier
        return build_identifier(tp, node);
    }
    
    // Handle parenthesized expressions
    uint32_t child_count = ts_node_child_count(node);
    if (child_count == 3) {
        TSNode first = ts_node_child(node, 0);
        StrView first_src = ts_node_source(tp, first);
        if (first_src.length == 1 && first_src.str[0] == '(') {
            return build_pattern_expr(tp, ts_node_child(node, 1));
        }
    }
    
    log_error("build_pattern_expr: unknown node symbol %d (%s)", symbol, ts_node_type(node));
    return nullptr;
}

// Build string/symbol pattern definition
AstNode* build_string_pattern(Transpiler* tp, TSNode node, bool is_symbol) {
    log_debug("build %s pattern definition", is_symbol ? "symbol" : "string");
    
    AstPatternDefNode* ast_node = (AstPatternDefNode*)
        alloc_ast_node(tp, is_symbol ? AST_NODE_SYMBOL_PATTERN : AST_NODE_STRING_PATTERN,
                       node, sizeof(AstPatternDefNode));
    ast_node->is_symbol = is_symbol;
    
    // Get pattern name
    TSNode name_node = ts_node_child_by_field_id(node, FIELD_NAME);
    int start_byte = ts_node_start_byte(name_node);
    StrView name_view = { .str = tp->source + start_byte, .length = ts_node_end_byte(name_node) - start_byte };
    ast_node->name = name_pool_create_strview(tp->name_pool, name_view);
    
    // Get pattern expression
    TSNode pattern_node = ts_node_child_by_field_id(node, FIELD_PATTERN);
    ast_node->as = build_pattern_expr(tp, pattern_node);
    
    // Type is pattern type
    TypePattern* pattern_type = (TypePattern*)alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
    pattern_type->is_symbol = is_symbol;
    pattern_type->pattern_index = -1;  // Will be set during compilation
    pattern_type->re2 = nullptr;       // Will be compiled during transpilation
    pattern_type->source = nullptr;
    ast_node->type = (Type*)pattern_type;
    
    // Register pattern name in current scope
    push_name(tp, (AstNamedNode*)ast_node, NULL);
    
    log_debug("built pattern definition: %.*s", (int)ast_node->name->len, ast_node->name->chars);
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
