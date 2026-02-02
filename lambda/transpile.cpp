#include "transpiler.hpp"
#include "safety_analyzer.hpp"
#include "re2_wrapper.hpp"
#include "../lib/log.h"
#include "../lib/hashmap.h"

extern Type TYPE_ANY, TYPE_INT;
void transpile_expr(Transpiler* tp, AstNode *expr_node);
void transpile_expr_tco(Transpiler* tp, AstNode *expr_node, bool in_tail_position);
void define_func(Transpiler* tp, AstFuncNode *fn_node, bool as_pointer);
void transpile_proc_content(Transpiler* tp, AstListNode *list_node);
Type* build_lit_string(Transpiler* tp, TSNode node, TSSymbol symbol);
Type* build_lit_datetime(Transpiler* tp, TSNode node, TSSymbol symbol);
void transpile_box_capture(Transpiler* tp, CaptureInfo* cap, bool from_outer_env);

// hashmap comparison and hashing functions for func_name_map
static int func_name_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(*(const char**)a, *(const char**)b);
}

static uint64_t func_name_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const char* name = *(const char**)item;
    return hashmap_sip(name, strlen(name), seed0, seed1);
}

// Register a function name mapping: MIR name -> Lambda name
// This is called during transpilation to build the name mapping table
// For closures/anonymous functions, defer registration until transpile_fn_expr where we know the assign name
static void register_func_name(Transpiler* tp, AstFuncNode* fn_node) {
    // For anonymous functions (no fn_node->name), don't register here
    // They will be registered in transpile_fn_expr with the assign name context
    if (!fn_node->name || !fn_node->name->chars) {
        return;
    }
    
    // create the map if it doesn't exist
    if (!tp->func_name_map) {
        tp->func_name_map = hashmap_new(sizeof(char*[2]), 64, 0, 0, func_name_hash, func_name_cmp, NULL, NULL);
    }
    
    // build MIR name (internal name like _outer36)
    StrBuf* mir_name_buf = strbuf_new_cap(64);
    write_fn_name(mir_name_buf, fn_node, NULL);
    char* mir_name = strdup(mir_name_buf->str);
    strbuf_free(mir_name_buf);
    
    // use the function name
    const char* lambda_name = fn_node->name->chars;
    
    // store in map: key is MIR name, value is Lambda name (strdup both for ownership)
    char* entry[2] = { mir_name, strdup(lambda_name) };
    hashmap_set(tp->func_name_map, entry);
    
    log_debug("register_func_name: '%s' -> '%s'", mir_name, lambda_name);
}

// Register a closure/anonymous function name at the point where we know its contextual name
static void register_func_name_with_context(Transpiler* tp, AstFuncNode* fn_node) {
    // create the map if it doesn't exist
    if (!tp->func_name_map) {
        tp->func_name_map = hashmap_new(sizeof(char*[2]), 64, 0, 0, func_name_hash, func_name_cmp, NULL, NULL);
    }
    
    // build MIR name (internal name like _f317)
    StrBuf* mir_name_buf = strbuf_new_cap(64);
    write_fn_name(mir_name_buf, fn_node, NULL);
    char* mir_name = strdup(mir_name_buf->str);
    strbuf_free(mir_name_buf);
    
    // determine Lambda name: prefer fn name, then current_assign_name, then <anonymous>
    const char* lambda_name = NULL;
    if (fn_node->name && fn_node->name->chars) {
        lambda_name = fn_node->name->chars;
    } else if (tp->current_assign_name && tp->current_assign_name->chars) {
        lambda_name = tp->current_assign_name->chars;
    } else {
        lambda_name = "<anonymous>";
    }
    
    // store in map: key is MIR name, value is Lambda name (strdup both for ownership)
    char* entry[2] = { mir_name, strdup(lambda_name) };
    hashmap_set(tp->func_name_map, entry);
    
    log_debug("register_func_name_with_context: '%s' -> '%s'", mir_name, lambda_name);
}

void write_node_source(Transpiler* tp, TSNode node) {
    int start_byte = ts_node_start_byte(node);
    const char* start = tp->source + start_byte;
    strbuf_append_str_n(tp->code_buf, start, ts_node_end_byte(node) - start_byte);
}

void write_fn_name(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import) {
    if (import) {
        strbuf_append_format(strbuf, "m%d.", import->script->index);
    }
    strbuf_append_char(strbuf, '_');
    if (fn_node->name && fn_node->name->chars) {
        strbuf_append_str_n(strbuf, fn_node->name->chars, fn_node->name->len);
    } else {
        strbuf_append_char(strbuf, 'f');
    }
    // char offset ensures the fn name is unique across the script
    strbuf_append_int(strbuf, ts_node_start_byte(fn_node->node));
    // no need to add param cnt
    // strbuf_append_char(tp->code_buf, '_');
    // strbuf_append_int(tp->code_buf, ((TypeFunc*)fn_node->type)->param_count);    
}

void write_var_name(StrBuf *strbuf, AstNamedNode *asn_node, AstImportNode* import) {
    if (import) {
        strbuf_append_format(strbuf, "m%d.", import->script->index);
    }
    // user var name starts with '_'
    strbuf_append_char(strbuf, '_');
    strbuf_append_str_n(strbuf, asn_node->name->chars, asn_node->name->len);
}

// write closure env struct name
void write_env_name(StrBuf *strbuf, AstFuncNode* fn_node) {
    strbuf_append_str(strbuf, "Env_f");
    strbuf_append_int(strbuf, ts_node_start_byte(fn_node->node));
}

// define closure environment struct for a function with captures
void define_closure_env(Transpiler* tp, AstFuncNode *fn_node) {
    if (!fn_node->captures) return;
    
    strbuf_append_str(tp->code_buf, "\ntypedef struct ");
    write_env_name(tp->code_buf, fn_node);
    strbuf_append_str(tp->code_buf, " {\n");
    
    // add each captured variable to the struct
    CaptureInfo* cap = fn_node->captures;
    while (cap) {
        strbuf_append_str(tp->code_buf, "  ");
        // use Item type for captured values (immutable capture by value)
        strbuf_append_str(tp->code_buf, "Item ");
        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
        strbuf_append_str(tp->code_buf, ";\n");
        cap = cap->next;
    }
    
    strbuf_append_str(tp->code_buf, "} ");
    write_env_name(tp->code_buf, fn_node);
    strbuf_append_str(tp->code_buf, ";\n");
}

// check if an identifier is a captured variable in the current closure
CaptureInfo* find_capture(AstFuncNode* closure, String* name) {
    if (!closure || !closure->captures) return nullptr;
    CaptureInfo* cap = closure->captures;
    while (cap) {
        if (cap->name->len == name->len && 
            strncmp(cap->name->chars, name->chars, name->len) == 0) {
            return cap;
        }
        cap = cap->next;
    }
    return nullptr;
}

// Forward declaration for recursive call
void pre_define_closure_envs(Transpiler* tp, AstNode* node);
void forward_declare_func(Transpiler* tp, AstFuncNode *fn_node);

// Recursively traverse AST and emit closure env struct definitions
// Also emits forward declarations for closure functions
void pre_define_closure_envs(Transpiler* tp, AstNode* node) {
    if (!node) return;
    
    switch (node->node_type) {
    case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR:  case AST_NODE_PROC: {
        AstFuncNode* fn = (AstFuncNode*)node;
        // Define this function's closure env if it has captures
        if (fn->captures) {
            define_closure_env(tp, fn);
            // Also emit a forward declaration for the closure function
            // so it can be referenced before its full definition
            forward_declare_func(tp, fn);
        }
        // Also check function's body and parameters for nested closures
        pre_define_closure_envs(tp, fn->body);
        AstNamedNode* param = fn->param;
        while (param) {
            pre_define_closure_envs(tp, (AstNode*)param);
            param = (AstNamedNode*)param->next;
        }
        break;
    }
    case AST_NODE_PRIMARY:
        pre_define_closure_envs(tp, ((AstPrimaryNode*)node)->expr);
        break;
    case AST_NODE_UNARY:
        pre_define_closure_envs(tp, ((AstUnaryNode*)node)->operand);
        break;
    case AST_NODE_BINARY:
        pre_define_closure_envs(tp, ((AstBinaryNode*)node)->left);
        pre_define_closure_envs(tp, ((AstBinaryNode*)node)->right);
        break;
    case AST_NODE_PIPE:
        pre_define_closure_envs(tp, ((AstPipeNode*)node)->left);
        pre_define_closure_envs(tp, ((AstPipeNode*)node)->right);
        break;
    case AST_NODE_CURRENT_ITEM:
    case AST_NODE_CURRENT_INDEX:
        // no children to process
        break;
    case AST_NODE_IF_EXPR:  case AST_NODE_IF_STAM: {
        AstIfNode* if_node = (AstIfNode*)node;
        pre_define_closure_envs(tp, if_node->cond);
        pre_define_closure_envs(tp, if_node->then);
        pre_define_closure_envs(tp, if_node->otherwise);
        break;
    }
    case AST_NODE_FOR_EXPR:  case AST_NODE_FOR_STAM: {
        AstForNode* for_node = (AstForNode*)node;
        pre_define_closure_envs(tp, (AstNode*)for_node->loop);
        pre_define_closure_envs(tp, for_node->then);
        break;
    }
    case AST_NODE_WHILE_STAM: {
        AstWhileNode* while_node = (AstWhileNode*)node;
        pre_define_closure_envs(tp, while_node->cond);
        pre_define_closure_envs(tp, while_node->body);
        break;
    }
    case AST_NODE_RETURN_STAM:
        pre_define_closure_envs(tp, ((AstReturnNode*)node)->value);
        break;
    case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM:  case AST_NODE_TYPE_STAM: {
        AstNode* decl = ((AstLetNode*)node)->declare;
        while (decl) {
            pre_define_closure_envs(tp, decl);
            decl = decl->next;
        }
        break;
    }
    case AST_NODE_ASSIGN:  case AST_NODE_KEY_EXPR:
        pre_define_closure_envs(tp, ((AstNamedNode*)node)->as);
        break;
    case AST_NODE_ASSIGN_STAM:
        pre_define_closure_envs(tp, ((AstAssignStamNode*)node)->value);
        break;
    case AST_NODE_LIST:  case AST_NODE_CONTENT: {
        AstListNode* list = (AstListNode*)node;
        AstNode* decl = list->declare;
        while (decl) {
            pre_define_closure_envs(tp, decl);
            decl = decl->next;
        }
        AstNode* item = list->item;
        while (item) {
            pre_define_closure_envs(tp, item);
            item = item->next;
        }
        break;
    }
    case AST_NODE_ARRAY: {
        AstNode* item = ((AstArrayNode*)node)->item;
        while (item) {
            pre_define_closure_envs(tp, item);
            item = item->next;
        }
        break;
    }
    case AST_NODE_MAP:  case AST_NODE_ELEMENT: {
        AstNode* item = ((AstMapNode*)node)->item;
        while (item) {
            pre_define_closure_envs(tp, item);
            item = item->next;
        }
        break;
    }
    case AST_NODE_CALL_EXPR: {
        pre_define_closure_envs(tp, ((AstCallNode*)node)->function);
        AstNode* arg = ((AstCallNode*)node)->argument;
        while (arg) {
            pre_define_closure_envs(tp, arg);
            arg = arg->next;
        }
        break;
    }
    case AST_NODE_MEMBER_EXPR:  case AST_NODE_INDEX_EXPR:
        pre_define_closure_envs(tp, ((AstFieldNode*)node)->object);
        pre_define_closure_envs(tp, ((AstFieldNode*)node)->field);
        break;
    default:
        break;
    }
}

// check if an AST node is an optional parameter reference (already Item type at runtime)
bool is_optional_param_ref(AstNode* item) {
    // unwrap nested PRIMARY nodes
    while (item->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)item;
        if (!pri->expr) return false;
        if (pri->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri->expr;
            if (!ident_node->entry || !ident_node->entry->node) return false;
            if (ident_node->entry->node->node_type != AST_NODE_PARAM) return false;
            TypeParam* param_type = (TypeParam*)ident_node->entry->node->type;
            return param_type->is_optional;
        }
        item = pri->expr;
    }
    return false;
}

// check if an AST node is a parameter reference in the current closure (already Item type at runtime)
bool is_closure_param_ref(Transpiler* tp, AstNode* item) {
    if (!tp->current_closure) return false;
    // unwrap nested PRIMARY nodes
    while (item->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)item;
        if (!pri->expr) return false;
        if (pri->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri->expr;
            if (!ident_node->entry || !ident_node->entry->node) return false;
            if (ident_node->entry->node->node_type != AST_NODE_PARAM) return false;
            // Make sure this param belongs to the CURRENT closure, not an outer function
            AstNamedNode* param = (AstNamedNode*)ident_node->entry->node;
            // Check if this parameter is in the current closure's parameter list
            AstNamedNode* closure_param = tp->current_closure->param;
            while (closure_param) {
                if (closure_param == param) return true;
                closure_param = (AstNamedNode*)closure_param->next;
            }
            return false;  // This is a param from an outer function (captured)
        }
        item = pri->expr;
    }
    return false;
}

// check if an AST node is a captured variable reference (stored as Item in closure env)
bool is_captured_var_ref(Transpiler* tp, AstNode* item) {
    if (!tp->current_closure) return false;
    // unwrap nested PRIMARY nodes
    while (item->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)item;
        if (!pri->expr) return false;
        if (pri->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri->expr;
            CaptureInfo* cap = find_capture(tp->current_closure, ident_node->name);
            return cap != nullptr;
        }
        item = pri->expr;
    }
    return false;
}

// emit env access for a captured variable (without unboxing) - returns true if successful
bool emit_captured_var_item(Transpiler* tp, AstNode* item) {
    if (!tp->current_closure) return false;
    // unwrap nested PRIMARY nodes
    while (item->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)item;
        if (!pri->expr) return false;
        if (pri->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri->expr;
            CaptureInfo* cap = find_capture(tp->current_closure, ident_node->name);
            if (cap) {
                // emit _env->varname directly (this is already an Item)
                strbuf_append_str(tp->code_buf, "_env->");
                strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                return true;
            }
            return false;
        }
        item = pri->expr;
    }
    return false;
}

// emit param reference as Item (without unboxing) for optional/default/closure params
// these params are stored as Item type at runtime, so emit _paramname directly
bool emit_param_item(Transpiler* tp, AstNode* item) {
    // unwrap nested PRIMARY nodes
    while (item->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)item;
        if (!pri->expr) return false;
        if (pri->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri->expr;
            if (!ident_node->entry || !ident_node->entry->node) return false;
            if (ident_node->entry->node->node_type != AST_NODE_PARAM) return false;
            TypeParam* param_type = (TypeParam*)ident_node->entry->node->type;
            // check if this param needs to be treated as Item (optional, default, or closure param)
            bool is_item_param = param_type->is_optional || param_type->default_value || tp->current_closure;
            if (is_item_param) {
                // emit _paramname directly (this is already an Item)
                strbuf_append_char(tp->code_buf, '_');
                strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                return true;
            }
            return false;
        }
        item = pri->expr;
    }
    return false;
}

void transpile_box_item(Transpiler* tp, AstNode *item) {
    if (!item->type) {
        log_debug("transpile box item: NULL type, node_type: %d", item->node_type);
        return;
    }

    switch (item->type->type_id) {
    case LMD_TYPE_NULL:
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
        } else {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, item);  // e.g. for procedural code
            strbuf_append_str(tp->code_buf, ",ITEM_NULL)");
        }
        break;
    case LMD_TYPE_BOOL:
        // Check if this is a closure parameter (already Item type at runtime)
        if (is_closure_param_ref(tp, item)) {
            emit_param_item(tp, item);  // emit _paramname directly (already an Item)
            break;
        }
        // Check if this is a captured variable reference (already Item in closure env)
        if (is_captured_var_ref(tp, item)) {
            emit_captured_var_item(tp, item);  // emit _env->varname directly (already an Item)
            break;
        }
        strbuf_append_str(tp->code_buf, "b2it(");
        transpile_expr(tp, item);
        strbuf_append_char(tp->code_buf, ')');
        break;
    case LMD_TYPE_INT: {
        // Check if this is an optional parameter or closure parameter (already Item type at runtime)
        if (is_optional_param_ref(item) || is_closure_param_ref(tp, item)) {
            emit_param_item(tp, item);  // emit _paramname directly (already an Item)
            break;
        }
        // Check if this is a captured variable reference (already Item in closure env)
        if (is_captured_var_ref(tp, item)) {
            emit_captured_var_item(tp, item);  // emit _env->varname directly (already an Item)
            break;
        }
        
        // Special case: if this is a binary expression with OPERATOR_POW, OPERATOR_DIV, OPERATOR_IDIV, or OPERATOR_MOD,
        // don't wrap with i2it because these functions already return an Item
        if (item->node_type == AST_NODE_BINARY) {
            AstBinaryNode* bin_node = (AstBinaryNode*)item;
            if (bin_node->op == OPERATOR_POW || bin_node->op == OPERATOR_DIV || 
                bin_node->op == OPERATOR_IDIV || bin_node->op == OPERATOR_MOD) {
                transpile_expr(tp, item);
                break;
            }
        }
        
        strbuf_append_str(tp->code_buf, "i2it(");
        transpile_expr(tp, item);
        strbuf_append_char(tp->code_buf, ')');
        break;
    }
    case LMD_TYPE_INT64: 
        // Check if this is an optional parameter or closure parameter (already Item type at runtime)
        if (is_optional_param_ref(item) || is_closure_param_ref(tp, item)) {
            emit_param_item(tp, item);  // emit _paramname directly (already an Item)
            break;
        }
        // Check if this is a captured variable reference (already Item in closure env)
        if (is_captured_var_ref(tp, item)) {
            emit_captured_var_item(tp, item);  // emit _env->varname directly (already an Item)
            break;
        }
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_l2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_str(tp->code_buf, ")");
        }
        else {
            log_enter();
            log_debug("transpile_box_item: push_l");
            strbuf_append_str(tp->code_buf, "push_l(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
            log_leave();
        }
        break;
    case LMD_TYPE_FLOAT: 
        // Check if this is an optional parameter or closure parameter (already Item type at runtime)
        if (is_optional_param_ref(item) || is_closure_param_ref(tp, item)) {
            emit_param_item(tp, item);  // emit _paramname directly (already an Item)
            break;
        }
        // Check if this is a captured variable reference (already Item in closure env)
        if (is_captured_var_ref(tp, item)) {
            emit_captured_var_item(tp, item);  // emit _env->varname directly (already an Item)
            break;
        }
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_d2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_char(tp->code_buf, ')');
        }
        else {
            strbuf_append_str(tp->code_buf, "push_d(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        break;
    case LMD_TYPE_DTIME: 
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_k2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_char(tp->code_buf, ')');
        }
        else {
            // non-literal datetime expression
            strbuf_append_str(tp->code_buf, "push_k(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');            
        }
        break;
    case LMD_TYPE_DECIMAL: 
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_c2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_char(tp->code_buf, ')');
        }
        else {
            // non-literal decimal expression
            strbuf_append_str(tp->code_buf, "c2it(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        break;
    case LMD_TYPE_NUMBER:
        // NUMBER type is a union of int/float - transpile the expression directly
        transpile_expr(tp, item);
        break;
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
        // Check if this is a captured variable reference (already Item in closure env)
        if (is_captured_var_ref(tp, item)) {
            emit_captured_var_item(tp, item);  // emit _env->varname directly (already an Item)
            break;
        }
        char t = item->type->type_id == LMD_TYPE_STRING ? 's' :
            item->type->type_id == LMD_TYPE_SYMBOL ? 'y' : 
            item->type->type_id == LMD_TYPE_BINARY ? 'x':'k';
        if (item->type->is_literal) {
            strbuf_append_format(tp->code_buf, "const_%c2it(", t);
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_str(tp->code_buf, ")");
        }
        else {
            strbuf_append_format(tp->code_buf, "%c2it(", t);
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        break;
    }
    case LMD_TYPE_LIST:
        transpile_expr(tp, item);  // list_end() -> Item 
        break;
    case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64:
    case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:  case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC:
        // All container types including Function* are direct pointers
        strbuf_append_str(tp->code_buf, "(Item)(");
        transpile_expr(tp, item);  // raw pointer treated as Item
        strbuf_append_char(tp->code_buf, ')');
        break;
    case LMD_TYPE_PATTERN:
        // Pattern is a pointer to TypePattern, which contains compiled RE2
        strbuf_append_str(tp->code_buf, "(Item)(");
        transpile_expr(tp, item);  // raw pointer treated as Item
        strbuf_append_char(tp->code_buf, ')');
        break;
    case LMD_TYPE_ANY:
    case LMD_TYPE_ERROR:
        // ANY and ERROR types are already Item at runtime - no boxing needed
        transpile_expr(tp, item);
        break;
    default:
        log_debug("unknown box item type: %d", item->type->type_id);
    }
}

void transpile_push_items(Transpiler* tp, AstNode *item, bool is_elmt) {
    while (item) {
        // skip let declaration and pattern definitions
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM || 
            item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_FUNC_EXPR || item->node_type == AST_NODE_PROC ||
            item->node_type == AST_NODE_STRING_PATTERN || item->node_type == AST_NODE_SYMBOL_PATTERN) {
            item = item->next;  continue;
        }
        // use list_push_spread to automatically spread spreadable arrays from for-expressions
        strbuf_append_format(tp->code_buf, "\n list_push_spread(%s, ", is_elmt ? "el" : "ls");
        transpile_box_item(tp, item);
        strbuf_append_str(tp->code_buf, ");");
        item = item->next;
    }
    strbuf_append_format(tp->code_buf, "\n list_end(%s);})", is_elmt ? "el" : "ls");
}

void transpile_primary_expr(Transpiler* tp, AstPrimaryNode *pri_node) {
    if (pri_node->expr) {
        if (pri_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri_node->expr;
            log_debug("transpile_primary_expr: identifier %.*s, type: %d", 
                (int)ident_node->name->len, ident_node->name->chars, pri_node->type->type_id);
            
            // check if this is a captured variable in the current closure
            if (tp->current_closure) {
                CaptureInfo* cap = find_capture(tp->current_closure, ident_node->name);
                if (cap) {
                    // access captured variable via env
                    // env stores Items, so we may need to unbox depending on usage
                    TypeId type_id = pri_node->type->type_id;
                    if (type_id == LMD_TYPE_INT) {
                        strbuf_append_str(tp->code_buf, "it2i(_env->");
                        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_INT64) {
                        strbuf_append_str(tp->code_buf, "it2l(_env->");
                        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_FLOAT) {
                        strbuf_append_str(tp->code_buf, "it2f(_env->");
                        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_BOOL) {
                        strbuf_append_str(tp->code_buf, "it2b(_env->");
                        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL || type_id == LMD_TYPE_BINARY) {
                        strbuf_append_str(tp->code_buf, "it2s(_env->");
                        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else {
                        // for Item or container types, return directly
                        strbuf_append_str(tp->code_buf, "_env->");
                        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                    }
                    return;
                }
            }
            
            AstNode* entry_node = ident_node->entry ? ident_node->entry->node : nullptr;
            
            // check if this is an optional/default parameter (needs unboxing since it's passed as Item)
            if (entry_node && entry_node->node_type == AST_NODE_PARAM) {
                TypeParam* param_type = (TypeParam*)entry_node->type;
                // Unbox if: (1) it's in a closure, OR (2) it has default value or is optional
                bool needs_unboxing = tp->current_closure || param_type->is_optional || param_type->default_value;
                
                if (needs_unboxing) {
                    // parameter passed as Item - needs unboxing to actual type
                    TypeId type_id = pri_node->type->type_id;
                    if (type_id == LMD_TYPE_INT) {
                        strbuf_append_str(tp->code_buf, "it2i(_");
                        strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_INT64) {
                        strbuf_append_str(tp->code_buf, "it2l(_");
                        strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_FLOAT) {
                        strbuf_append_str(tp->code_buf, "it2f(_");
                        strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_BOOL) {
                        strbuf_append_str(tp->code_buf, "it2b(_");
                        strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL || type_id == LMD_TYPE_BINARY) {
                        strbuf_append_str(tp->code_buf, "it2s(_");
                        strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else {
                        // for Item or container types, return directly (already Item)
                        strbuf_append_char(tp->code_buf, '_');
                        strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                    }
                    return;
                }
            }
            
            if (entry_node) {
                if (entry_node->node_type == AST_NODE_FUNC || entry_node->node_type == AST_NODE_FUNC_EXPR || entry_node->node_type == AST_NODE_PROC) {
                    // Function reference - check if it's a closure
                    AstFuncNode* fn_node = (AstFuncNode*)entry_node;
                    TypeFunc* fn_type = fn_node->type ? (TypeFunc*)fn_node->type : nullptr;
                    int arity = fn_type ? fn_type->param_count : 0;
                    
                    if (fn_node->captures) {
                        // This is a closure - need to create the env and use to_closure
                        // Generate same code as transpile_fn_expr for closures
                        strbuf_append_str(tp->code_buf, "({ ");
                        write_env_name(tp->code_buf, fn_node);
                        strbuf_append_str(tp->code_buf, "* _closure_env = heap_calloc(sizeof(");
                        write_env_name(tp->code_buf, fn_node);
                        strbuf_append_str(tp->code_buf, "), 0);\n");
                        
                        // populate captured variables
                        CaptureInfo* cap = fn_node->captures;
                        while (cap) {
                            strbuf_append_str(tp->code_buf, "  _closure_env->");
                            strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                            strbuf_append_str(tp->code_buf, " = ");
                            
                            // box the captured variable
                            bool from_outer = false;
                            if (tp->current_closure) {
                                CaptureInfo* outer_cap = find_capture(tp->current_closure, cap->name);
                                from_outer = (outer_cap != nullptr);
                            }
                            transpile_box_capture(tp, cap, from_outer);
                            strbuf_append_str(tp->code_buf, ";\n");
                            cap = cap->next;
                        }
                        
                        strbuf_append_str(tp->code_buf, "  to_closure_named(");
                        write_fn_name(tp->code_buf, fn_node, (AstImportNode*)ident_node->entry->import);
                        strbuf_append_format(tp->code_buf, ",%d,_closure_env,", arity);
                        // pass function name as string literal for stack traces
                        // prefer named function, fall back to assignment variable name, then <anonymous>
                        if (fn_node->name && fn_node->name->chars) {
                            strbuf_append_char(tp->code_buf, '"');
                            strbuf_append_str_n(tp->code_buf, fn_node->name->chars, fn_node->name->len);
                            strbuf_append_char(tp->code_buf, '"');
                        } else if (tp->current_assign_name && tp->current_assign_name->chars) {
                            strbuf_append_char(tp->code_buf, '"');
                            strbuf_append_str_n(tp->code_buf, tp->current_assign_name->chars, tp->current_assign_name->len);
                            strbuf_append_char(tp->code_buf, '"');
                        } else {
                            strbuf_append_str(tp->code_buf, "\"<anonymous>\"");
                        }
                        strbuf_append_str(tp->code_buf, "); })");
                    } else {
                        // Regular function without captures - use to_fn_named for stack traces
                        strbuf_append_format(tp->code_buf, "to_fn_named(");
                        write_fn_name(tp->code_buf, fn_node, 
                            (AstImportNode*)ident_node->entry->import);
                        strbuf_append_format(tp->code_buf, ",%d,", arity);
                        // pass function name as string literal for stack traces
                        if (fn_node->name && fn_node->name->chars) {
                            strbuf_append_char(tp->code_buf, '"');
                            strbuf_append_str_n(tp->code_buf, fn_node->name->chars, fn_node->name->len);
                            strbuf_append_char(tp->code_buf, '"');
                        } else if (tp->current_assign_name && tp->current_assign_name->chars) {
                            strbuf_append_char(tp->code_buf, '"');
                            strbuf_append_str_n(tp->code_buf, tp->current_assign_name->chars, tp->current_assign_name->len);
                            strbuf_append_char(tp->code_buf, '"');
                        } else {
                            strbuf_append_str(tp->code_buf, "\"<anonymous>\"");
                        }
                        strbuf_append_char(tp->code_buf, ')');
                    }
                }
                else if (entry_node->node_type == AST_NODE_STRING_PATTERN || entry_node->node_type == AST_NODE_SYMBOL_PATTERN) {
                    // Pattern reference - emit const_pattern call
                    AstPatternDefNode* pattern_def = (AstPatternDefNode*)entry_node;
                    TypePattern* pattern_type = (TypePattern*)pattern_def->type;
                    log_debug("transpile_primary_expr: pattern reference '%.*s', index=%d",
                        (int)ident_node->name->len, ident_node->name->chars, pattern_type->pattern_index);
                    strbuf_append_format(tp->code_buf, "const_pattern(%d)", pattern_type->pattern_index);
                }
                else {
                    log_debug("transpile_primary_expr: writing var name for %.*s, entry type: %d",
                        (int)ident_node->name->len, ident_node->name->chars,
                        ident_node->entry->node->type->type_id);
                    
                    // For decimal identifiers, we need to convert the pointer to an Item
                    if (ident_node->entry->node->type->type_id == LMD_TYPE_DECIMAL) {
                        strbuf_append_str(tp->code_buf, "c2it(");
                        write_var_name(tp->code_buf, (AstNamedNode*)ident_node->entry->node, 
                            (AstImportNode*)ident_node->entry->import);
                        strbuf_append_char(tp->code_buf, ')');
                    } else {
                        write_var_name(tp->code_buf, (AstNamedNode*)ident_node->entry->node, 
                            (AstImportNode*)ident_node->entry->import);
                    }
                }
            }
            else {
                // handle the case where entry is null - identifier is undefined
                // emit error value instead of raw identifier name to prevent crash
                log_error("Error: undefined identifier '%.*s'", (int)ident_node->name->len, ident_node->name->chars);
                tp->error_count++;
                strbuf_append_str(tp->code_buf, "ItemError");
            }
        } else { 
            transpile_expr(tp, pri_node->expr);
        }
    } else { // const
        log_debug("transpile_primary_expr: const");
        if (pri_node->type->is_literal) {  // literal
            if (pri_node->type->type_id == LMD_TYPE_STRING || pri_node->type->type_id == LMD_TYPE_SYMBOL ||
                pri_node->type->type_id == LMD_TYPE_BINARY) {
                // loads the const string without boxing
                strbuf_append_str(tp->code_buf, "const_s(");
                TypeString *str_type = (TypeString*)pri_node->type;
                strbuf_append_int(tp->code_buf, str_type->const_index);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (pri_node->type->type_id == LMD_TYPE_DTIME) {
                // loads the const datetime without boxing
                strbuf_append_str(tp->code_buf, "const_k(");
                TypeDateTime *dt_type = (TypeDateTime*)pri_node->type;
                strbuf_append_int(tp->code_buf, dt_type->const_index);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (pri_node->type->type_id == LMD_TYPE_INT) {
                write_node_source(tp, pri_node->node);
                // int32 literals don't use 'L' suffix
            }
            else if (pri_node->type->type_id == LMD_TYPE_INT64) {
                write_node_source(tp, pri_node->node);
                strbuf_append_char(tp->code_buf, 'L');  // add 'L' to ensure it is a long
            }
            else if (pri_node->type->type_id == LMD_TYPE_FLOAT) {
                // we have large int that is promoted to float, thus needs the casting to double
                strbuf_append_str(tp->code_buf, "((double)(");
                write_node_source(tp, pri_node->node);
                strbuf_append_str(tp->code_buf, "))");
            }
            else if (pri_node->type->type_id == LMD_TYPE_DECIMAL) {
                // loads the const decimal without boxing
                strbuf_append_str(tp->code_buf, "const_c2it(");
                TypeDecimal *dec_type = (TypeDecimal*)pri_node->type;
                strbuf_append_int(tp->code_buf, dec_type->const_index);
                strbuf_append_char(tp->code_buf, ')');
            }
            else { // bool, null, float
                TSNode child = ts_node_named_child(pri_node->node, 0);
                TSSymbol symbol = ts_node_symbol(child);
                write_node_source(tp, pri_node->node);
            }
        } else {
            write_node_source(tp, pri_node->node);
        }
    }
}

void transpile_unary_expr(Transpiler* tp, AstUnaryNode *unary_node) {
    log_debug("transpile unary expr");
    if (unary_node->op == OPERATOR_NOT) {
        TypeId operand_type = unary_node->operand->type->type_id;
        if (operand_type == LMD_TYPE_BOOL) {
            // direct C negation for boolean values
            strbuf_append_str(tp->code_buf, "!");
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, unary_node->operand);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            strbuf_append_str(tp->code_buf, "fn_not(");
            transpile_box_item(tp, unary_node->operand);  
            strbuf_append_str(tp->code_buf, ")");
        }
    }
    else if (unary_node->op == OPERATOR_POS || unary_node->op == OPERATOR_NEG) {
        TypeId operand_type = unary_node->operand->type->type_id;
        
        // Fast path for numeric types that can be handled directly by C
        if (operand_type == LMD_TYPE_INT || operand_type == LMD_TYPE_INT64 || 
            operand_type == LMD_TYPE_FLOAT) {
            // Direct C operator application for primitive numeric types
            if (unary_node->op == OPERATOR_POS) {
                // Unary + can be optimized away for numeric types
                strbuf_append_char(tp->code_buf, '(');
                transpile_expr(tp, unary_node->operand);
                strbuf_append_char(tp->code_buf, ')');
            } else { // OPERATOR_NEG
                // two brackets to prevent '-' joining into '--'
                strbuf_append_str(tp->code_buf, "(-(");
                transpile_expr(tp, unary_node->operand);
                strbuf_append_str(tp->code_buf, "))");
            }
        }
        else {
            // Runtime function call for other types (ANY, DECIMAL, etc.)
            if (unary_node->op == OPERATOR_POS) {
                strbuf_append_str(tp->code_buf, "fn_pos(");
            } else { // OPERATOR_NEG
                strbuf_append_str(tp->code_buf, "fn_neg(");
            }
            transpile_box_item(tp, unary_node->operand);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else {
        // Fallback for unknown operators (should not happen with proper AST)
        log_error("Error: transpile_unary_expr unknown operator %d", unary_node->op);
        strbuf_append_str(tp->code_buf, "null");
    }
}

void transpile_binary_expr(Transpiler* tp, AstBinaryNode *bi_node) {
    TypeId left_type = bi_node->left->type->type_id;
    TypeId right_type = bi_node->right->type->type_id;     
    if (bi_node->op == OPERATOR_AND || bi_node->op == OPERATOR_OR) {
        // Check if we need type error checking for mixed types
        if (left_type != LMD_TYPE_BOOL || right_type != LMD_TYPE_BOOL) {
            if (bi_node->op == OPERATOR_AND) {
                strbuf_append_str(tp->code_buf, "fn_and(");
            } else {
                strbuf_append_str(tp->code_buf, "fn_or(");
            }
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // slightly faster path
            if (bi_node->op == OPERATOR_AND) {
                strbuf_append_str(tp->code_buf, "op_and(");
            } else {
                strbuf_append_str(tp->code_buf, "op_or(");
            }
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');            
        }
    }
    else if (bi_node->op == OPERATOR_POW) {
        strbuf_append_str(tp->code_buf, "fn_pow(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_ADD) {
        if (left_type == right_type) {
            if (left_type == LMD_TYPE_INT || left_type == LMD_TYPE_INT64 || left_type == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "(");
                transpile_expr(tp, bi_node->left);
                strbuf_append_char(tp->code_buf, '+');
                transpile_expr(tp, bi_node->right);
                strbuf_append_char(tp->code_buf, ')');
                return;
            }
            // else let fn_add() handle it
        }
        else if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_FLOAT) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, '+');
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }
        // call runtime fn_add()
        strbuf_append_str(tp->code_buf, "fn_add(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_SUB) {
        if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_FLOAT) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, '-');
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }
        // call runtime fn_sub()
        strbuf_append_str(tp->code_buf, "fn_sub(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_MUL) {
        if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_FLOAT) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, '*');
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }
        // call runtime fn_mul()
        strbuf_append_str(tp->code_buf, "fn_mul(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_MOD) {
        // Always call runtime fn_mod() for proper error handling (division by zero, type checking)
        strbuf_append_str(tp->code_buf, "fn_mod(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_DIV) {
        if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_FLOAT) {
            strbuf_append_str(tp->code_buf, "((double)(");
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, ")/(double)(");
            transpile_expr(tp, bi_node->right);
            strbuf_append_str(tp->code_buf, "))");
            return;
        }
        // call runtime fn_div()
        strbuf_append_str(tp->code_buf, "fn_div(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_IDIV) {
        // Always call runtime fn_idiv() for proper error handling
        strbuf_append_str(tp->code_buf, "fn_idiv(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_IS) {
        strbuf_append_str(tp->code_buf, "fn_is(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');   
    }
    else if (bi_node->op == OPERATOR_IN) {
        strbuf_append_str(tp->code_buf, "fn_in(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');   
    }
    else if (bi_node->op == OPERATOR_TO) {
        strbuf_append_str(tp->code_buf, "fn_to(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');   
    }
    else if (bi_node->op == OPERATOR_EQ) {
        // TypeId left_type = bi_node->left->type->type_id;
        // TypeId right_type = bi_node->right->type->type_id;
        // because of encoding of error value, it is very hard to have a fast path
        bool has_fast_path = false; // (left_type == right_type && left_type != LMD_TYPE_STRING);
        if (!has_fast_path) {
            strbuf_append_str(tp->code_buf, "fn_eq(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use direct C comparison for compatible types
            // todo: need to split into cases, and handle diff type differently
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " == ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_NE) {
        // TypeId left_type = bi_node->left->type->type_id;
        // TypeId right_type = bi_node->right->type->type_id;
        // because of encoding of error value, it is very hard to have a fast path
        bool has_fast_path = false; // (left_type == right_type && left_type != LMD_TYPE_STRING);        
        if (!has_fast_path) {
            strbuf_append_str(tp->code_buf, "fn_ne(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use direct C comparison for compatible non-string types
            // todo: need to split into cases, and handle diff type differently
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " != ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_LT) {
        // TypeId left_type = bi_node->left->type->type_id;
        // TypeId right_type = bi_node->right->type->type_id;
        // because of encoding of error value, it is very hard to have a fast path
        bool has_fast_path = false; // (left_type == right_type && left_type != LMD_TYPE_STRING);
        if (!has_fast_path) {
            strbuf_append_str(tp->code_buf, "fn_lt(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use direct C comparison for compatible types
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " < ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_LE) {
        // Check if we need to use the runtime comparison function for type checking
        // TypeId left_type = bi_node->left->type->type_id;
        // TypeId right_type = bi_node->right->type->type_id;
        // because of encoding of error value, it is very hard to have a fast path
        bool has_fast_path = false; // (left_type == right_type && left_type != LMD_TYPE_STRING);
        if (!has_fast_path) {
            strbuf_append_str(tp->code_buf, "fn_le(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use direct C comparison for compatible types
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " <= ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_GT) {
        // Check if we need to use the runtime comparison function for type checking
        // TypeId left_type = bi_node->left->type->type_id;
        // TypeId right_type = bi_node->right->type->type_id;
        // because of encoding of error value, it is very hard to have a fast path
        bool has_fast_path = false; // (left_type == right_type && left_type != LMD_TYPE_STRING);
        if (!has_fast_path) {
            strbuf_append_str(tp->code_buf, "fn_gt(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use direct C comparison for compatible types
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " > ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_GE) {
        // Check if we need to use the runtime comparison function for type checking
        // TypeId left_type = bi_node->left->type->type_id;
        // TypeId right_type = bi_node->right->type->type_id;
        // because of encoding of error value, it is very hard to have a fast path
        bool has_fast_path = false; // (left_type == right_type && left_type != LMD_TYPE_STRING);
        if (!has_fast_path) {
            strbuf_append_str(tp->code_buf, "fn_ge(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use direct C comparison for compatible types
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " >= ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    } 
    else if (bi_node->op == OPERATOR_JOIN) { 
        strbuf_append_str(tp->code_buf, "fn_join(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else {
        log_error("Error: unknown binary operator %d", bi_node->op);
        strbuf_append_str(tp->code_buf, "null");  // should be error
    }
}

// for both if_expr and if_stam
void transpile_if(Transpiler* tp, AstIfNode *if_node) {
    log_debug("transpile if expr");
    
    // Check types for proper conditional expression handling
    Type* if_type = if_node->type;
    Type* then_type = if_node->then ? if_node->then->type : nullptr;
    Type* else_type = if_node->otherwise ? if_node->otherwise->type : nullptr;
    
    strbuf_append_str(tp->code_buf, "(");
    if (if_node->cond->type && if_node->cond->type->type_id == LMD_TYPE_BOOL) {
        transpile_expr(tp, if_node->cond);
    }
    else {
        strbuf_append_str(tp->code_buf, "is_truthy(");
        transpile_box_item(tp, if_node->cond);
        strbuf_append_char(tp->code_buf, ')');
    }
    strbuf_append_str(tp->code_buf, " ? ");

    // Determine if branches have incompatible types that need coercion
    bool need_boxing = true;
    if (then_type && else_type && (then_type->type_id == else_type->type_id) && then_type->type_id != LMD_TYPE_ANY) {
        need_boxing = false;
    }
    if (need_boxing) {
        log_debug("transpile if expr with boxing");
        if (if_node->then) {
            transpile_box_item(tp, if_node->then);
        } else {
            strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        }
        strbuf_append_str(tp->code_buf, " : ");
        if (if_node->otherwise) {
            transpile_box_item(tp, if_node->otherwise);
        } else {
            // otherwise is optional
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
        }
        strbuf_append_str(tp->code_buf, ")");
    } else {
        // fast path without boxing
        log_debug("transpile if expr without boxing");
        transpile_expr(tp, if_node->then);
        strbuf_append_char(tp->code_buf, ':');
        if (if_node->otherwise) {
            transpile_expr(tp, if_node->otherwise);
        } else {
            log_warn("Warning: if_stam missing else clause");
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
        }
        strbuf_append_char(tp->code_buf, ')');
    }
    log_debug("end if expr");
}

void transpile_assign_expr(Transpiler* tp, AstNamedNode *asn_node, bool is_global = false) {
    log_debug("transpile assign expr");
    // defensive validation: ensure all required pointers and components are valid
    if (!asn_node || !asn_node->type || !asn_node->as) {
        log_error("Error: asn_node is invalid");
        strbuf_append_str(tp->code_buf, "error");
        return;
    }
    
    // declare the variable type
    Type *var_type = asn_node->type;
    strbuf_append_str(tp->code_buf, "\n ");
    if (!is_global) {
        write_type(tp->code_buf, var_type);
        strbuf_append_char(tp->code_buf, ' ');
    }
    write_var_name(tp->code_buf, asn_node, NULL);
    strbuf_append_char(tp->code_buf, '=');

    // set assignment name context for closure naming
    String* prev_assign_name = tp->current_assign_name;
    tp->current_assign_name = asn_node->name;
    
    transpile_expr(tp, asn_node->as);
    
    // restore previous context
    tp->current_assign_name = prev_assign_name;
    
    strbuf_append_char(tp->code_buf, ';');
}

// Transpile decomposition: let a, b = expr OR let a, b at expr
void transpile_decompose_expr(Transpiler* tp, AstDecomposeNode *dec_node, bool is_global = false) {
    log_debug("transpile decompose expr, name_count=%d, is_named=%d", dec_node->name_count, dec_node->is_named);
    
    if (!dec_node || !dec_node->as || dec_node->name_count == 0) {
        log_error("Error: invalid decompose node");
        return;
    }
    
    // For local scope: declare variables first, then use nested scope for temp
    if (!is_global) {
        // Declare all variables outside the helper scope
        for (int i = 0; i < dec_node->name_count; i++) {
            String* name = dec_node->names[i];
            strbuf_append_str(tp->code_buf, "\n Item _");
            strbuf_append_str_n(tp->code_buf, name->chars, name->len);
            strbuf_append_str(tp->code_buf, ";");
        }
    }
    
    // Use a nested scope to avoid _dec_src redeclaration conflicts
    strbuf_append_str(tp->code_buf, "\n {Item _dec_src=");
    transpile_box_item(tp, dec_node->as);
    strbuf_append_str(tp->code_buf, ";");
    
    // Assign values inside the nested scope
    for (int i = 0; i < dec_node->name_count; i++) {
        String* name = dec_node->names[i];
        strbuf_append_str(tp->code_buf, "\n _");
        strbuf_append_str_n(tp->code_buf, name->chars, name->len);
        strbuf_append_char(tp->code_buf, '=');
        
        if (dec_node->is_named) {
            // Named decomposition: get attribute by name
            strbuf_append_str(tp->code_buf, "item_attr(_dec_src,\"");
            strbuf_append_str_n(tp->code_buf, name->chars, name->len);
            strbuf_append_str(tp->code_buf, "\");");
        } else {
            // Positional decomposition: get by index
            strbuf_append_format(tp->code_buf, "item_at(_dec_src,%d);", i);
        }
    }
    strbuf_append_str(tp->code_buf, "}");  // close the nested scope
}

void transpile_let_stam(Transpiler* tp, AstLetNode *let_node, bool is_global = false) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!let_node) { log_error("Error: missing let_node");  return; }
    
    AstNode *declare = let_node->declare;
    while (declare) {
        // Handle both regular assignments and decompositions
        if (declare->node_type == AST_NODE_ASSIGN) {
            transpile_assign_expr(tp, (AstNamedNode*)declare, is_global);
        } else if (declare->node_type == AST_NODE_DECOMPOSE) {
            transpile_decompose_expr(tp, (AstDecomposeNode*)declare, is_global);
        } else {
            log_error("Error: transpile_let_stam found unexpected node type %d in declare chain", declare->node_type);
        }
        declare = declare->next;
    }
}

void transpile_loop_expr(Transpiler* tp, AstLoopNode *loop_node, AstNode* then, bool use_array) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!loop_node || !loop_node->as || !loop_node->as->type || !then) {
        log_error("Error: invalid loop_node");
        return;
    }
    Type * expr_type = loop_node->as->type;
    Type *item_type = nullptr;
    bool is_named = loop_node->is_named;  // 'at' keyword for attribute/named iteration
    
    // determine loop item type based on expression type and iteration mode
    if (is_named) {
        // 'at' iteration: iterating over attributes/fields
        item_type = &TYPE_ANY;  // attribute values are always ANY
    } else if (expr_type->type_id == LMD_TYPE_ARRAY) {
        TypeArray* array_type = (TypeArray*)expr_type;
        // Validate that the cast is safe by checking the nested pointer
        if (array_type && array_type->nested && (uintptr_t)array_type->nested > 0x1000) {
            item_type = array_type->nested;
        } else {
            log_warn("Warning: Invalid nested type in array, using TYPE_ANY");
            item_type = &TYPE_ANY;
        }
    } else if (expr_type->type_id == LMD_TYPE_RANGE) {
        item_type = &TYPE_INT;
    } else {
        item_type = &TYPE_ANY;
    }
        
    if (!item_type) {
        log_error("Error: transpile_loop_expr failed to determine item type");
        item_type = &TYPE_ANY; // fallback to ANY type for safety
    }

    // Helper: generate index variable declaration if present
    bool has_index = loop_node->index_name != nullptr;
    
    if (is_named) {
        // 'at' iteration: iterate over attributes/fields
        // for k, v at expr -> k is key name, v is value
        // for v at expr -> v is key name (single variable form)
        strbuf_append_str(tp->code_buf, " Item it=");
        transpile_box_item(tp, loop_node->as);
        strbuf_append_str(tp->code_buf, ";\n ArrayList* _attr_keys=item_keys(it);\n");
        strbuf_append_str(tp->code_buf, " for (int _ki=0; _attr_keys && _ki<_attr_keys->length; _ki++) {\n");
        
        if (has_index) {
            // Two-variable form: for k, v at expr
            // k (index_name) = key name, v (name) = value
            strbuf_append_str(tp->code_buf, "  String* _");
            strbuf_append_str_n(tp->code_buf, loop_node->index_name->chars, loop_node->index_name->len);
            strbuf_append_str(tp->code_buf, "=_attr_keys->data[_ki];\n");
            
            strbuf_append_str(tp->code_buf, "  Item _");
            strbuf_append_str_n(tp->code_buf, loop_node->name->chars, loop_node->name->len);
            strbuf_append_str(tp->code_buf, "=item_attr(it, _");
            strbuf_append_str_n(tp->code_buf, loop_node->index_name->chars, loop_node->index_name->len);
            strbuf_append_str(tp->code_buf, "->chars);\n");
        } else {
            // Single-variable form: for v at expr -> v is key name
            strbuf_append_str(tp->code_buf, "  String* _");
            strbuf_append_str_n(tp->code_buf, loop_node->name->chars, loop_node->name->len);
            strbuf_append_str(tp->code_buf, "=_attr_keys->data[_ki];\n");
        }
    } else {
        // 'in' iteration: standard indexed iteration
        strbuf_append_str(tp->code_buf, 
            expr_type->type_id == LMD_TYPE_RANGE ? " Range *rng=" :
            item_type->type_id == LMD_TYPE_INT ? " ArrayInt *arr=" :
            item_type->type_id == LMD_TYPE_INT64 ? " ArrayInt64 *arr=" :
            item_type->type_id == LMD_TYPE_FLOAT ? " ArrayFloat *arr=" :
            expr_type->type_id == LMD_TYPE_ARRAY ? " Array *arr=" : 
            " Item it=");
        transpile_expr(tp, loop_node->as);

        // start the loop
        strbuf_append_str(tp->code_buf, 
            expr_type->type_id == LMD_TYPE_RANGE ? ";\n if (!rng) { array_push(arr_out, ITEM_ERROR); } else { for (long _idx=rng->start; _idx<=rng->end; _idx++) {\n " : 
            expr_type->type_id == LMD_TYPE_ARRAY ? ";\n if (!arr) { array_push(arr_out, ITEM_ERROR); } else { for (int _idx=0; _idx<arr->length; _idx++) {\n " :
            ";\n int ilen = fn_len(it);\n for (int _idx=0; _idx<ilen; _idx++) {\n ");

        // generate index variable if present (for i, v in expr)
        if (has_index) {
            strbuf_append_str(tp->code_buf, "  long _");
            strbuf_append_str_n(tp->code_buf, loop_node->index_name->chars, loop_node->index_name->len);
            strbuf_append_str(tp->code_buf, "=_idx;\n");
        }

        // construct loop variable
        write_type(tp->code_buf, item_type);
        strbuf_append_str(tp->code_buf, " _");
        strbuf_append_str_n(tp->code_buf, loop_node->name->chars, loop_node->name->len);
        if (expr_type->type_id == LMD_TYPE_RANGE) {
            strbuf_append_str(tp->code_buf, "=_idx;\n");
        } 
        else if (expr_type->type_id == LMD_TYPE_ARRAY) {
            if (item_type->type_id == LMD_TYPE_STRING) {
                strbuf_append_str(tp->code_buf, "=fn_string(arr->items[_idx]);\n");
            } 
            else {
                strbuf_append_str(tp->code_buf, "=arr->items[_idx];\n");
            }        
        }
        else {
            strbuf_append_str(tp->code_buf, "=item_at(it,_idx);\n");
        }
    }

    // nested loop variables
    AstNode *next_loop = loop_node->next;
    if (next_loop) {
        log_debug("transpile nested loop");
        log_enter();
        transpile_loop_expr(tp, (AstLoopNode*)next_loop, then, use_array);
        log_leave();
    } 
    else { // loop body
        Type *then_type = then->type;
        // push to array (spreadable) or list
        if (use_array) {
            strbuf_append_str(tp->code_buf, " array_push(arr_out,");
        } else {
            strbuf_append_str(tp->code_buf, " list_push(ls,");
        }
        transpile_box_item(tp, then);
        strbuf_append_str(tp->code_buf, ");");
    }
    // end the loop
    if (!is_named && (expr_type->type_id == LMD_TYPE_RANGE || expr_type->type_id == LMD_TYPE_ARRAY)) {
        strbuf_append_char(tp->code_buf, '}');
    }
    strbuf_append_str(tp->code_buf, " }\n");
}

// both for_expr and for_stam
void transpile_for(Transpiler* tp, AstForNode *for_node) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!for_node || !for_node->then || !for_node->then->type) {
        log_error("Error: invalid for_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    Type *then_type = for_node->then->type;
    // init a spreadable array for for-expression results
    strbuf_append_str(tp->code_buf, "({\n Array* arr_out=array_spreadable(); \n");
    AstNode *loop = for_node->loop;
    if (loop) {
        transpile_loop_expr(tp, (AstLoopNode*)loop, for_node->then, true);
    }
    // return the array
    strbuf_append_str(tp->code_buf, " array_end(arr_out);})");
}

// forward declaration
bool has_current_item_ref(AstNode* node);

// pipe expression: data | transform or data where condition
// Semantics:
// - With ~ in right side: auto-map over collection (or apply to scalar)
// - Without ~: pass whole collection as first argument to function
// - where: filter items where condition is truthy
// Implementation: inline the iteration loop similar to for-expression
void transpile_pipe_expr(Transpiler* tp, AstPipeNode *pipe_node) {
    log_debug("transpile pipe expr");
    if (!pipe_node || !pipe_node->left || !pipe_node->right) {
        log_error("Error: invalid pipe_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    
    TypeId left_type = pipe_node->left->type ? pipe_node->left->type->type_id : LMD_TYPE_ANY;
    bool uses_current_item = has_current_item_ref(pipe_node->right);
    
    if (!uses_current_item && pipe_node->op == OPERATOR_PIPE) {
        // aggregate pipe: pass whole collection as first argument
        // For now, evaluate left and right, call fn_pipe_call
        strbuf_append_str(tp->code_buf, "fn_pipe_call(");
        transpile_box_item(tp, pipe_node->left);
        strbuf_append_str(tp->code_buf, ", ");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    
    // Uses ~ or ~# - need to iterate
    // Generate inline loop using statement expression
    // Use array instead of list to avoid string merging behavior
    strbuf_append_str(tp->code_buf, "({\n");
    strbuf_append_str(tp->code_buf, "  Item _pipe_collection = ");
    transpile_box_item(tp, pipe_node->left);
    strbuf_append_str(tp->code_buf, ";\n");
    strbuf_append_str(tp->code_buf, "  TypeId _pipe_type = item_type_id(_pipe_collection);\n");
    strbuf_append_str(tp->code_buf, "  Array* _pipe_result = array();\n");
    
    // Check if collection type - if not, apply to single item
    strbuf_append_str(tp->code_buf, "  if (_pipe_type == LMD_TYPE_ARRAY || _pipe_type == LMD_TYPE_LIST || ");
    strbuf_append_str(tp->code_buf, "_pipe_type == LMD_TYPE_RANGE || _pipe_type == LMD_TYPE_MAP || ");
    strbuf_append_str(tp->code_buf, "_pipe_type == LMD_TYPE_ARRAY_INT || _pipe_type == LMD_TYPE_ARRAY_INT64 || ");
    strbuf_append_str(tp->code_buf, "_pipe_type == LMD_TYPE_ARRAY_FLOAT || _pipe_type == LMD_TYPE_ELEMENT) {\n");
    
    // Map case - iterate over key-value pairs
    strbuf_append_str(tp->code_buf, "    if (_pipe_type == LMD_TYPE_MAP) {\n");
    strbuf_append_str(tp->code_buf, "      ArrayList* _pipe_keys = item_keys(_pipe_collection);\n");
    strbuf_append_str(tp->code_buf, "      if (_pipe_keys) {\n");
    strbuf_append_str(tp->code_buf, "        for (int64_t _pipe_i = 0; _pipe_i < _pipe_keys->length; _pipe_i++) {\n");
    strbuf_append_str(tp->code_buf, "          String* _key_str = (String*)_pipe_keys->data[_pipe_i];\n");
    strbuf_append_str(tp->code_buf, "          Item _pipe_index = s2it(_key_str);\n");
    strbuf_append_str(tp->code_buf, "          Item _pipe_item = item_attr(_pipe_collection, _key_str->chars);\n");
    
    if (pipe_node->op == OPERATOR_WHERE) {
        // filter - only keep if condition is truthy
        strbuf_append_str(tp->code_buf, "          if (is_truthy(");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_str(tp->code_buf, ")) {\n");
        strbuf_append_str(tp->code_buf, "            array_push(_pipe_result, _pipe_item);\n");
        strbuf_append_str(tp->code_buf, "          }\n");
    } else {
        // map - transform and collect
        strbuf_append_str(tp->code_buf, "          array_push(_pipe_result, ");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_str(tp->code_buf, ");\n");
    }
    strbuf_append_str(tp->code_buf, "        }\n");
    strbuf_append_str(tp->code_buf, "        // Note: _pipe_keys memory managed by heap GC\n");
    strbuf_append_str(tp->code_buf, "      }\n");
    strbuf_append_str(tp->code_buf, "    } else {\n");
    
    // Array/List/Range case - iterate with numeric index
    strbuf_append_str(tp->code_buf, "      int64_t _pipe_len = fn_len(_pipe_collection);\n");
    strbuf_append_str(tp->code_buf, "      for (int64_t _pipe_i = 0; _pipe_i < _pipe_len; _pipe_i++) {\n");
    strbuf_append_str(tp->code_buf, "        Item _pipe_index = i2it(_pipe_i);\n");
    strbuf_append_str(tp->code_buf, "        Item _pipe_item = item_at(_pipe_collection, (int)_pipe_i);\n");
    
    if (pipe_node->op == OPERATOR_WHERE) {
        // filter
        strbuf_append_str(tp->code_buf, "        if (is_truthy(");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_str(tp->code_buf, ")) {\n");
        strbuf_append_str(tp->code_buf, "          array_push(_pipe_result, _pipe_item);\n");
        strbuf_append_str(tp->code_buf, "        }\n");
    } else {
        // map
        strbuf_append_str(tp->code_buf, "        array_push(_pipe_result, ");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_str(tp->code_buf, ");\n");
    }
    strbuf_append_str(tp->code_buf, "      }\n");
    strbuf_append_str(tp->code_buf, "    }\n");
    strbuf_append_str(tp->code_buf, "  } else {\n");
    
    // Scalar case - apply transform once
    strbuf_append_str(tp->code_buf, "    Item _pipe_item = _pipe_collection;\n");
    strbuf_append_str(tp->code_buf, "    Item _pipe_index = ITEM_NULL;\n");
    
    if (pipe_node->op == OPERATOR_WHERE) {
        strbuf_append_str(tp->code_buf, "    if (is_truthy(");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_str(tp->code_buf, ")) {\n");
        strbuf_append_str(tp->code_buf, "      array_push(_pipe_result, _pipe_item);\n");
        strbuf_append_str(tp->code_buf, "    }\n");
    } else {
        strbuf_append_str(tp->code_buf, "    array_push(_pipe_result, ");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_str(tp->code_buf, ");\n");
    }
    strbuf_append_str(tp->code_buf, "  }\n");
    
    // Return result - array_end finalizes and returns as Item
    strbuf_append_str(tp->code_buf, "  array_end(_pipe_result);\n");
    strbuf_append_str(tp->code_buf, "})");
}

// while statement (procedural only)
void transpile_while(Transpiler* tp, AstWhileNode *while_node) {
    log_debug("transpile while stam");
    if (!while_node || !while_node->cond || !while_node->body) {
        log_error("Error: invalid while_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    
    strbuf_append_str(tp->code_buf, "while (");
    if (while_node->cond->type && while_node->cond->type->type_id == LMD_TYPE_BOOL) {
        transpile_expr(tp, while_node->cond);
    } else {
        strbuf_append_str(tp->code_buf, "is_truthy(");
        transpile_box_item(tp, while_node->cond);
        strbuf_append_char(tp->code_buf, ')');
    }
    strbuf_append_str(tp->code_buf, ") {\n");
    // use procedural content for while body
    if (while_node->body->node_type == AST_NODE_CONTENT) {
        transpile_proc_content(tp, (AstListNode*)while_node->body);
    } else {
        transpile_expr(tp, while_node->body);
    }
    strbuf_append_str(tp->code_buf, ";\n}");
}

// procedural if statement - generates C-style if/else blocks
// unlike transpile_if which generates ternary expressions, this supports
// statements like break, continue, return in the branches
void transpile_if_stam(Transpiler* tp, AstIfNode *if_node) {
    log_debug("transpile if stam (procedural)");
    if (!if_node || !if_node->cond) {
        log_error("Error: invalid if_node");
        return;
    }
    
    strbuf_append_str(tp->code_buf, "if (");
    if (if_node->cond->type && if_node->cond->type->type_id == LMD_TYPE_BOOL) {
        transpile_expr(tp, if_node->cond);
    } else {
        strbuf_append_str(tp->code_buf, "is_truthy(");
        transpile_box_item(tp, if_node->cond);
        strbuf_append_char(tp->code_buf, ')');
    }
    strbuf_append_str(tp->code_buf, ") {");
    
    // transpile then branch as procedural content
    if (if_node->then) {
        if (if_node->then->node_type == AST_NODE_CONTENT) {
            transpile_proc_content(tp, (AstListNode*)if_node->then);
        } else if (if_node->then->node_type == AST_NODE_IF_STAM) {
            // nested if in then branch
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_if_stam(tp, (AstIfNode*)if_node->then);
        } else {
            // single expression - just execute it
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_expr(tp, if_node->then);
            strbuf_append_char(tp->code_buf, ';');
        }
    }
    strbuf_append_str(tp->code_buf, "\n}");
    
    // transpile else branch if present
    if (if_node->otherwise) {
        strbuf_append_str(tp->code_buf, " else {");
        if (if_node->otherwise->node_type == AST_NODE_CONTENT) {
            transpile_proc_content(tp, (AstListNode*)if_node->otherwise);
        } else if (if_node->otherwise->node_type == AST_NODE_IF_STAM) {
            // else if chain
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_if_stam(tp, (AstIfNode*)if_node->otherwise);
        } else {
            // single expression
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_expr(tp, if_node->otherwise);
            strbuf_append_char(tp->code_buf, ';');
        }
        strbuf_append_str(tp->code_buf, "\n}");
    }
}

// return statement (procedural only)
void transpile_return(Transpiler* tp, AstReturnNode *return_node) {
    log_debug("transpile return stam");
    strbuf_append_str(tp->code_buf, "return ");
    if (return_node->value) {
        transpile_box_item(tp, return_node->value);
    } else {
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
    }
    strbuf_append_char(tp->code_buf, ';');
}

// assignment statement for mutable variables (procedural only)
void transpile_assign_stam(Transpiler* tp, AstAssignStamNode *assign_node) {
    log_debug("transpile assign stam");
    if (!assign_node || !assign_node->target || !assign_node->value) {
        log_error("Error: invalid assign_node");
        return;
    }
    
    strbuf_append_str(tp->code_buf, "\n _");  // add underscore prefix for variable name
    strbuf_append_str_n(tp->code_buf, assign_node->target->chars, assign_node->target->len);
    strbuf_append_char(tp->code_buf, '=');
    transpile_expr(tp, assign_node->value);
    strbuf_append_char(tp->code_buf, ';');
}

void transpile_items(Transpiler* tp, AstNode *item) {
    bool is_first = true;
    while (item) {
        // skip let declaration and pattern definitions
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM || 
            item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_FUNC_EXPR || item->node_type == AST_NODE_PROC ||
            item->node_type == AST_NODE_STRING_PATTERN || item->node_type == AST_NODE_SYMBOL_PATTERN) {
            item = item->next;  continue;
        }
        if (is_first) { is_first = false; } 
        else { strbuf_append_str(tp->code_buf, ", "); }
        transpile_box_item(tp, item);
        item = item->next;
    }
}

// check if any item in the list/array needs spreading (for-expression or spread operator)
static bool has_spreadable_item(AstNode *item) {
    while (item) {
        if (item->node_type == AST_NODE_FOR_EXPR) {
            return true;
        }
        // TODO: also check for spread operator when implemented
        item = item->next;
    }
    return false;
}

void transpile_array_expr(Transpiler* tp, AstArrayNode *array_node) {
    TypeArray *type = (TypeArray*)array_node->type;
    bool is_int_array = type->nested && type->nested->type_id == LMD_TYPE_INT;
    bool is_int64_array = type->nested && type->nested->type_id == LMD_TYPE_INT64;
    bool is_float_array = type->nested && type->nested->type_id == LMD_TYPE_FLOAT;
    
    // for arrays with spreadable items (for-expressions), use push path
    if (!is_int_array && !is_int64_array && !is_float_array && has_spreadable_item(array_node->item)) {
        strbuf_append_str(tp->code_buf, "({\n Array* arr = array();\n");
        AstNode* item = array_node->item;
        while (item) {
            strbuf_append_str(tp->code_buf, " array_push_spread(arr, ");
            transpile_box_item(tp, item);
            strbuf_append_str(tp->code_buf, ");\n");
            item = item->next;
        }
        strbuf_append_str(tp->code_buf, " (Item)arr; })");
        return;
    }
    
    if (is_int_array) {
        strbuf_append_str(tp->code_buf, "({ArrayInt* arr = array_int(); array_int_fill(arr,");
    } else if (is_int64_array) {
        strbuf_append_str(tp->code_buf, "({ArrayInt64* arr = array_int64(); array_int64_fill(arr,");
    } else if (is_float_array) {
        strbuf_append_str(tp->code_buf, "({ArrayFloat* arr = array_float(); array_float_fill(arr,");
    } else {
        strbuf_append_str(tp->code_buf, "({Array* arr = array(); array_fill(arr,");
    }
    
    strbuf_append_int(tp->code_buf, type->length);
    // only add comma if there are items to follow
    if (array_node->item) {
        strbuf_append_char(tp->code_buf, ',');
    }
    
    if (is_int_array || is_int64_array || is_float_array) {
        AstNode *item = array_node->item;
        while (item) {
            // transpile as unboxed items
            transpile_expr(tp, item);
            if (item->next) {
                strbuf_append_char(tp->code_buf, ',');
            }
            item = item->next;
        }
    } else {
        // transpile as boxed items
        transpile_items(tp, array_node->item);
    }
    strbuf_append_str(tp->code_buf, "); })");
}

void transpile_list_expr(Transpiler* tp, AstListNode *list_node) {
    log_debug("transpile list expr: dec - %p, itm - %p", list_node->declare, list_node->item);
    // Defensive validation: ensure all required pointers and components are valid
    if (!list_node || !list_node->type || !list_node->list_type) {
        log_error("Error: invalid list_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    
    TypeArray *type = list_node->list_type;
    log_debug("transpile_list_expr: type->length = %ld", type->length);
    // create list before the declarations, to contain all the allocations
    strbuf_append_str(tp->code_buf, "({\n List* ls = list();\n");
    // let declare first
    AstNode *declare = list_node->declare;
    while (declare) {
        if (declare->node_type != AST_NODE_ASSIGN) {
            log_error("Error: transpile_list_expr found non-assign node in declare chain");
            // Skip invalid node - defensive recovery
            declare = declare->next;
            continue;
        }
        transpile_assign_expr(tp, (AstNamedNode*)declare);
        strbuf_append_str(tp->code_buf, "\n");  // ensure declarations are on separate lines
        declare = declare->next;
    }
    if (type->length == 0) {
        log_debug("transpile_list_expr: type->length is 0, outputting null");
        strbuf_append_str(tp->code_buf, " list_end(ls);})");
        return;
    }
    // use push path if there are spreadable items (for-expressions) or many items
    if (type->length < 10 && !has_spreadable_item(list_node->item)) {
        strbuf_append_str(tp->code_buf, "\n list_fill(ls,");
        strbuf_append_int(tp->code_buf, type->length);
        strbuf_append_char(tp->code_buf, ',');
        transpile_items(tp, list_node->item);
        strbuf_append_str(tp->code_buf, ");})");
    } 
    else {
        transpile_push_items(tp, list_node->item, false);
    }
}
void transpile_proc_content(Transpiler* tp, AstListNode *list_node) {
    log_debug("transpile proc content");
    if (!list_node) { log_error("Error: missing list_node");  return; }
    
    AstNode *item = list_node->item;
    AstNode *last_item = NULL;
    
    // find the last non-declaration item for return value
    AstNode *scan = item;
    while (scan) {
        if (scan->node_type != AST_NODE_LET_STAM && scan->node_type != AST_NODE_PUB_STAM &&
            scan->node_type != AST_NODE_TYPE_STAM && scan->node_type != AST_NODE_FUNC &&
            scan->node_type != AST_NODE_FUNC_EXPR && scan->node_type != AST_NODE_PROC &&
            scan->node_type != AST_NODE_VAR_STAM &&
            scan->node_type != AST_NODE_STRING_PATTERN && scan->node_type != AST_NODE_SYMBOL_PATTERN) {
            last_item = scan;
        }
        scan = scan->next;
    }
    
    strbuf_append_str(tp->code_buf, "({\n Item result = ITEM_NULL;");
    
    while (item) {
        // handle declarations
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_VAR_STAM) {
            transpile_let_stam(tp, (AstLetNode*)item, false);
        }
        else if (item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM ||
                 item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_FUNC_EXPR ||
                 item->node_type == AST_NODE_PROC ||
                 item->node_type == AST_NODE_STRING_PATTERN || item->node_type == AST_NODE_SYMBOL_PATTERN) {
            // skip - already handled globally or pattern definitions
        }
        else if (item->node_type == AST_NODE_WHILE_STAM) {
            transpile_while(tp, (AstWhileNode*)item);
        }
        else if (item->node_type == AST_NODE_BREAK_STAM) {
            strbuf_append_str(tp->code_buf, "\n break;");
        }
        else if (item->node_type == AST_NODE_CONTINUE_STAM) {
            strbuf_append_str(tp->code_buf, "\n continue;");
        }
        else if (item->node_type == AST_NODE_RETURN_STAM) {
            transpile_return(tp, (AstReturnNode*)item);
        }
        else if (item->node_type == AST_NODE_ASSIGN_STAM) {
            transpile_assign_stam(tp, (AstAssignStamNode*)item);
        }
        else if (item->node_type == AST_NODE_FOR_STAM) {
            transpile_for(tp, (AstForNode*)item);
        }
        else if (item->node_type == AST_NODE_IF_STAM) {
            // procedural if statement - use C-style if/else blocks
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_if_stam(tp, (AstIfNode*)item);
        }
        else if (item->node_type == AST_NODE_IF_EXPR) {
            // if expression - wrap value (ternary style)
            if (item == last_item) {
                strbuf_append_str(tp->code_buf, "\n result = ");
            } else {
                strbuf_append_str(tp->code_buf, "\n ");
            }
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ';');
        }
        else if (item->node_type == AST_NODE_CALL_EXPR) {
            // call expression - capture result if last
            if (item == last_item) {
                strbuf_append_str(tp->code_buf, "\n result = ");
            } else {
                strbuf_append_str(tp->code_buf, "\n ");
            }
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ';');
        }
        else {
            // other expressions - capture if last
            if (item == last_item) {
                strbuf_append_str(tp->code_buf, "\n result = ");
                transpile_box_item(tp, item);
                strbuf_append_char(tp->code_buf, ';');
            } else {
                strbuf_append_str(tp->code_buf, "\n ");
                transpile_expr(tp, item);
                strbuf_append_char(tp->code_buf, ';');
            }
        }
        item = item->next;
    }
    
    strbuf_append_str(tp->code_buf, "\n result;})" );
}

void transpile_content_expr(Transpiler* tp, AstListNode *list_node, bool is_global = false) {
    log_debug("transpile content expr");
    TypeArray *type = list_node->list_type;
    // create list before the declarations, to contain all the allocations
    strbuf_append_str(tp->code_buf, "({\n List* ls = list();");
    // let declare first
    AstNode *item = list_node->item;
    int effective_length = type->length;
    while (item) {
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM) {
            effective_length--;
            transpile_let_stam(tp, (AstLetNode*)item, is_global);
        }
        else if (item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_FUNC_EXPR || item->node_type == AST_NODE_PROC) {
            effective_length--;
            // already transpiled
        }
        item = item->next;
    }
    if (effective_length == 0) {
        strbuf_append_str(tp->code_buf, "list_end(ls);})");
        return;
    }
    transpile_push_items(tp, list_node->item, false);
}

void transpile_map_expr(Transpiler* tp, AstMapNode *map_node) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!map_node) {
        log_error("Error: transpile_map_expr called with null map node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    if (!map_node->type) {
        log_error("Error: transpile_map_expr missing type information");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    
    strbuf_append_str(tp->code_buf, "({Map* m = map(");
    strbuf_append_int(tp->code_buf, ((TypeMap*)map_node->type)->type_index);
    strbuf_append_str(tp->code_buf, ");");
    AstNode *item = map_node->item;
    if (item) {
        strbuf_append_str(tp->code_buf, "\n map_fill(m,");
        while (item) {
            if (item->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* key_expr = (AstNamedNode*)item;
                if (key_expr->as) {
                    transpile_box_item(tp, key_expr->as);  // use box_item to wrap with i2it() etc.
                } else {
                    log_error("Error: transpile_map_expr key expression missing assignment");
                    strbuf_append_str(tp->code_buf, "ITEM_ERROR");
                }
            } else {
                transpile_box_item(tp, item);
            }
            if (item->next) { strbuf_append_char(tp->code_buf, ','); }
            item = item->next;
        }
        strbuf_append_str(tp->code_buf, ");");
    }
    else {
        strbuf_append_str(tp->code_buf, "m;");
    }
    strbuf_append_str(tp->code_buf, "})");
}

void transpile_element(Transpiler* tp, AstElementNode *elmt_node) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!elmt_node) {
        log_error("Error: transpile_element called with null element node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    if (!elmt_node->type) {
        log_error("Error: transpile_element missing type information");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    
    strbuf_append_str(tp->code_buf, "\n({Element* el=elmt(");
    TypeElmt* type = (TypeElmt*)elmt_node->type;
    strbuf_append_int(tp->code_buf, type->type_index);
    strbuf_append_str(tp->code_buf, ");");

    // transpile the attributes
    AstNode *item = elmt_node->item;
    if (item) {
        strbuf_append_str(tp->code_buf, "\n elmt_fill(el,");
        while (item) {
            if (item->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* key_expr = (AstNamedNode*)item;
                if (key_expr->as) {
                    transpile_box_item(tp, key_expr->as);  // use box_item to wrap with i2it() etc.
                } else {
                    log_error("Error: transpile_element key expression missing assignment");
                    strbuf_append_str(tp->code_buf, "ITEM_ERROR");
                }
            } else {
                transpile_box_item(tp, item);
            }
            if (item->next) { strbuf_append_char(tp->code_buf, ','); }
            item = item->next;
        }
        strbuf_append_str(tp->code_buf, ");");
    }

    // transpile the content items
    if (type->content_length) {
        if (type->content_length < 10) {
            strbuf_append_str(tp->code_buf, "\n list_fill(el,");
            strbuf_append_int(tp->code_buf, type->content_length);
            strbuf_append_char(tp->code_buf, ',');
            if (elmt_node->content) {
                transpile_items(tp, ((AstListNode*)elmt_node->content)->item);
            } else {
                log_error("Error: transpile_element content missing despite content_length > 0");
                strbuf_append_str(tp->code_buf, "ITEM_ERROR");
            }
            strbuf_append_str(tp->code_buf, ");})");
        } else {
            if (elmt_node->content) {
                transpile_push_items(tp, ((AstListNode*)elmt_node->content)->item, true);
            } else {
                log_error("Error: transpile_element content missing despite content_length > 0");
                strbuf_append_str(tp->code_buf, "ITEM_ERROR");
            }
        }
    }
    else { // no content
        if (elmt_node->item) {
            strbuf_append_str(tp->code_buf, " list_end(el);})"); 
        }
        else { // and no attr, thus no frame_end
            strbuf_append_str(tp->code_buf, " el;})"); 
        }  
    }
}

// helper function to transpile a single argument with type coercion
void transpile_call_argument(Transpiler* tp, AstNode* arg, TypeParam* param_type, bool is_sys_func) {
    if (!arg) {
        // use default value if available
        if (param_type && param_type->default_value) {
            log_debug("using default value for param type=%d, default type=%d", 
                param_type->type_id, param_type->default_value->type->type_id);
            // for optional params with default, box since function expects Item type
            if (param_type->is_optional) {
                transpile_box_item(tp, param_type->default_value);
            } else if (param_type->type_id == LMD_TYPE_ANY) {
                transpile_box_item(tp, param_type->default_value);
            } else {
                transpile_expr(tp, param_type->default_value);
            }
        } else {
            // No default value - use null for missing optional parameters
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
        }
        return;
    }

    // For named arguments, get the actual value
    AstNode* value = arg;
    if (arg->node_type == AST_NODE_NAMED_ARG) {
        AstNamedNode* named_arg = (AstNamedNode*)arg;
        value = named_arg->as;
    }

    log_debug("transpile_call_argument: type=%d, node_type=%d", 
        value->type ? value->type->type_id : -1, value->node_type);

    // For system functions, box DateTime arguments
    if (is_sys_func && value->type->type_id == LMD_TYPE_DTIME) {
        log_debug("transpile_call_argument: BOXING DateTime for sys func");
        strbuf_append_str(tp->code_buf, "k2it(");
        transpile_expr(tp, value);
        strbuf_append_str(tp->code_buf, ")");
    }
    // for optional params, always box to Item since function expects Item type
    else if (param_type && param_type->is_optional) {
        transpile_box_item(tp, value);
    }
    // boxing based on arg type and fn definition type
    else if (param_type) {
        if (param_type->type_id == value->type->type_id) {
            transpile_expr(tp, value);
        }
        else if (param_type->type_id == LMD_TYPE_FLOAT) {
            if ((value->type->type_id == LMD_TYPE_INT || value->type->type_id == LMD_TYPE_INT64 || 
                value->type->type_id == LMD_TYPE_FLOAT)) {
                transpile_expr(tp, value);
            }
            else if (value->type->type_id == LMD_TYPE_ANY) {
                strbuf_append_str(tp->code_buf, "it2d(");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else {
                strbuf_append_str(tp->code_buf, "null");
            }
        }
        else if (param_type->type_id == LMD_TYPE_INT64) {
            if (value->type->type_id == LMD_TYPE_INT || value->type->type_id == LMD_TYPE_INT64) {
                transpile_expr(tp, value);
            }
            else if (value->type->type_id == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "((int64_t)");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (value->type->type_id == LMD_TYPE_ANY) {
                strbuf_append_str(tp->code_buf, "it2l(");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else {
                log_error("Error: incompatible argument type for int64 parameter");
                strbuf_append_str(tp->code_buf, "null");
            }
        }
        else if (param_type->type_id == LMD_TYPE_INT) {
            if (value->type->type_id == LMD_TYPE_INT) {
                transpile_expr(tp, value);
            }
            else if (value->type->type_id == LMD_TYPE_INT64 || value->type->type_id == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "((int32_t)");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (value->type->type_id == LMD_TYPE_ANY) {
                strbuf_append_str(tp->code_buf, "it2i(");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else {
                log_error("Error: incompatible argument type for int parameter");
                strbuf_append_str(tp->code_buf, "null");
            }
        }
        else {
            transpile_box_item(tp, value);
        }
    }
    else {
        transpile_box_item(tp, value);
    }
}

// find parameter by name in a function's parameter list
AstNamedNode* find_param_by_name(AstFuncNode* fn_node, String* name) {
    if (!name) return NULL;
    AstNamedNode* param = fn_node->param;
    while (param) {
        if (param->name && strcmp(param->name->chars, name->chars) == 0) {
            return param;
        }
        param = (AstNamedNode*)param->next;
    }
    return NULL;
}

// get parameter at index
AstNamedNode* get_param_at_index(AstFuncNode* fn_node, int index) {
    AstNamedNode* param = fn_node->param;
    for (int i = 0; i < index && param; i++) {
        param = (AstNamedNode*)param->next;
    }
    return param;
}

/**
 * Transpile a tail-recursive call as a goto statement.
 * Transforms: factorial(n-1, acc*n) into:
 *   { int _t0 = _n - 1; int _t1 = _acc * _n; _n = _t0; _acc = _t1; goto _tco_start; }
 * 
 * Note: We use temporary variables to handle cases like f(b, a) where args are swapped.
 */
void transpile_tail_call(Transpiler* tp, AstCallNode* call_node, AstFuncNode* tco_func) {
    log_debug("transpile_tail_call: converting recursive call to goto");
    
    // Generate: ({ temp assignments; param = temp; ...; goto _tco_start; ITEM_NULL; })
    // The ITEM_NULL at end is never reached but satisfies the expression type
    strbuf_append_str(tp->code_buf, "({ ");
    
    // Count params and args
    int param_count = 0;
    AstNamedNode* param = tco_func->param;
    while (param) { param_count++; param = (AstNamedNode*)param->next; }
    
    // First pass: assign arguments to temporary variables
    // This handles cases like factorial(b, a) where we swap values
    AstNode* arg = call_node->argument;
    param = tco_func->param;
    int arg_idx = 0;
    
    while (arg && param) {
        // Generate: TypeX _tN = <arg>;
        Type* param_type = param->type;
        write_type(tp->code_buf, param_type);
        strbuf_append_format(tp->code_buf, " _tco_tmp%d = ", arg_idx);
        transpile_expr(tp, arg);
        strbuf_append_str(tp->code_buf, "; ");
        
        arg = arg->next;
        param = (AstNamedNode*)param->next;
        arg_idx++;
    }
    
    // Second pass: assign temporaries to parameters
    param = tco_func->param;
    for (int i = 0; i < arg_idx; i++) {
        if (!param) break;
        strbuf_append_str(tp->code_buf, "_");
        strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
        strbuf_append_format(tp->code_buf, " = _tco_tmp%d; ", i);
        param = (AstNamedNode*)param->next;
    }
    
    // Jump back to function start
    strbuf_append_str(tp->code_buf, "goto _tco_start; ");
    
    // The expression type - never reached but keeps C happy
    Type* ret_type = ((TypeFunc*)tco_func->type)->returned;
    if (!ret_type) ret_type = &TYPE_ANY;
    if (ret_type->type_id == LMD_TYPE_INT) {
        strbuf_append_str(tp->code_buf, "0; })");
    } else if (ret_type->type_id == LMD_TYPE_FLOAT) {
        strbuf_append_str(tp->code_buf, "0.0; })");
    } else if (ret_type->type_id == LMD_TYPE_BOOL) {
        strbuf_append_str(tp->code_buf, "false; })");
    } else {
        strbuf_append_str(tp->code_buf, "ITEM_NULL; })");
    }
}

/**
 * Check if a call expression is a tail-recursive call to the TCO function.
 */
bool is_tco_tail_call(Transpiler* tp, AstCallNode* call_node) {
    if (!tp->tco_func || !tp->in_tail_position) return false;
    return is_recursive_call(call_node, tp->tco_func);
}

void transpile_call_expr(Transpiler* tp, AstCallNode *call_node) {
    log_debug("transpile call expr");
    // Defensive validation: ensure all required pointers and components are valid
    if (!call_node || !call_node->function || !call_node->function->type) {
        log_error("Error: invalid call_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    
    // TCO: Check if this is a tail-recursive call that should be transformed to goto
    if (is_tco_tail_call(tp, call_node)) {
        transpile_tail_call(tp, call_node, tp->tco_func);
        return;
    }

    // write the function name/ptr
    TypeFunc *fn_type = NULL;
    AstFuncNode* fn_node = NULL;  // used for named args lookup
    bool is_sys_func = (call_node->function->node_type == AST_NODE_SYS_FUNC);
    bool is_fn_variable = false;  // true if calling through a function variable (need fn_call)
    bool is_direct_call = true;   // true if we can use direct function call

    if (is_sys_func) {
        AstSysFuncNode* sys_fn_node = (AstSysFuncNode*)call_node->function;
        // Use the sys func name from fn_info, not from TSNode source
        // This correctly handles method-style calls (obj.method()) which are desugared to sys func calls
        strbuf_append_str(tp->code_buf, sys_fn_node->fn_info->is_proc ? "pn_" : "fn_");
        strbuf_append_str(tp->code_buf, sys_fn_node->fn_info->name);
        if (sys_fn_node->fn_info->is_overloaded) { strbuf_append_int(tp->code_buf, sys_fn_node->fn_info->arg_count); }
    }
    else {
        TypeId callee_type_id = call_node->function->type ? call_node->function->type->type_id : LMD_TYPE_NULL;
        
        // Check if callee is a parameter or variable that should be treated as a function
        // even if it doesn't have explicit function type (e.g., let add10 = make_adder(10); add10(5))
        bool is_callable_param = false;
        bool is_callable_variable = false;  // variable that might hold a function at runtime
        bool is_callable_call_expr = false; // callee is a call expression (e.g., make_adder(10)(5))
        AstPrimaryNode *primary_fn_node = call_node->function->node_type == AST_NODE_PRIMARY ? 
            (AstPrimaryNode*)call_node->function : null;
        
        // Early check for undefined function call - prevents crash
        if (primary_fn_node && primary_fn_node->expr && primary_fn_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)primary_fn_node->expr;
            if (!ident_node->entry) {
                // Calling an undefined identifier - emit error
                log_error("Error: call to undefined function '%.*s'", 
                    (int)ident_node->name->len, ident_node->name->chars);
                tp->error_count++;
                strbuf_append_str(tp->code_buf, "ItemError");
                return;
            }
        }
        
        if (primary_fn_node && primary_fn_node->expr && primary_fn_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)primary_fn_node->expr;
            AstNode* entry_node = ident_node->entry ? ident_node->entry->node : NULL;
            if (entry_node && entry_node->node_type == AST_NODE_PARAM) {
                // This is a parameter being used as a callable
                is_callable_param = true;
            }
            else if (entry_node && entry_node->node_type == AST_NODE_ASSIGN) {
                // This is a let variable being called - might hold a function at runtime
                // We can't statically know it's a function, but we'll use fn_call dynamically
                is_callable_variable = true;
                log_debug("callable variable (let) detected: %.*s", 
                    (int)ident_node->name->len, ident_node->name->chars);
            }
        }
        
        // Check if callee is a call expression (chained call like make_adder(10)(5))
        // Note: call expressions may be wrapped in a primary expression
        if (call_node->function->node_type == AST_NODE_CALL_EXPR) {
            is_callable_call_expr = true;
            log_debug("callable call expression detected (direct)");
        }
        else if (primary_fn_node && primary_fn_node->expr && 
                 primary_fn_node->expr->node_type == AST_NODE_CALL_EXPR) {
            is_callable_call_expr = true;
            log_debug("callable call expression detected (wrapped in primary)");
        }
        
        if (callee_type_id == LMD_TYPE_FUNC || is_callable_param || is_callable_variable || is_callable_call_expr) {
            if (callee_type_id == LMD_TYPE_FUNC) {
                fn_type = (TypeFunc*)call_node->function->type;
            }
            if (primary_fn_node && primary_fn_node->expr && primary_fn_node->expr->node_type == AST_NODE_IDENT) {
                AstIdentNode* ident_node = (AstIdentNode*)primary_fn_node->expr;
                AstNode* entry_node = ident_node->entry ? ident_node->entry->node : NULL;
                if (entry_node && (entry_node->node_type == AST_NODE_FUNC || 
                    entry_node->node_type == AST_NODE_FUNC_EXPR || entry_node->node_type == AST_NODE_PROC)) {
                    // Direct function reference - use direct call
                    fn_node = (AstFuncNode*)entry_node;  // save for named args lookup
                    write_fn_name(tp->code_buf, fn_node, 
                        (AstImportNode*)ident_node->entry->import);
                } else if (entry_node && entry_node->node_type == AST_NODE_PARAM) {
                    // Function parameter - use fn_call for dynamic dispatch
                    is_fn_variable = true;
                    is_direct_call = false;
                    log_debug("callable parameter detected: %.*s", 
                        (int)ident_node->name->len, ident_node->name->chars);
                } else { 
                    // Variable holding a function - use fn_call for dynamic dispatch
                    is_fn_variable = true;
                    is_direct_call = false;
                    log_debug("function variable detected: %.*s", 
                        (int)ident_node->name->len, ident_node->name->chars);
                }
            }
            else if (call_node->function->node_type == AST_NODE_INDEX_EXPR ||
                     call_node->function->node_type == AST_NODE_MEMBER_EXPR) {
                // Function from array/map access - need dynamic call
                is_fn_variable = true;
                is_direct_call = false;
                log_debug("function from index/member expression");
            }
            else {
                // Other expression returning function - use dynamic call
                is_fn_variable = true;
                is_direct_call = false;
                log_debug("function from expression");
            }
            
            // For anonymous functions referenced directly (not through variable), use ->ptr
            if (fn_type && fn_type->is_anonymous && !is_fn_variable) {
                transpile_expr(tp, call_node->function);
                strbuf_append_str(tp->code_buf, "->ptr");
            }
        } else { // handle Item
            log_debug("call function type is not func");
            strbuf_append_str(tp->code_buf, "ITEM_ERROR");
            return;
        }       
    }

    // count arguments and check for named arguments
    int arg_count = 0;
    bool has_named_args = false;
    AstNode* arg = call_node->argument;
    while (arg) {
        if (arg->node_type == AST_NODE_NAMED_ARG) {
            has_named_args = true;
        }
        arg_count++;
        arg = arg->next;
    }

    int expected_count = fn_type ? fn_type->param_count : -1;
    bool is_variadic = fn_type ? fn_type->is_variadic : false;

    // For function variables, use fn_callN() for efficiency or fn_call() for many args
    if (is_fn_variable) {
        // Get the function pointer expression
        AstPrimaryNode *primary_fn_node = call_node->function->node_type == AST_NODE_PRIMARY ? 
            (AstPrimaryNode*)call_node->function : null;
        
        // Check if callee is a parameter with Item type (needs extraction from tagged pointer)
        bool needs_item_extraction = false;
        AstNode* callee_entry_node = nullptr;
        if (primary_fn_node && primary_fn_node->expr && primary_fn_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)primary_fn_node->expr;
            callee_entry_node = ident_node->entry ? ident_node->entry->node : nullptr;
            if (callee_entry_node && callee_entry_node->node_type == AST_NODE_PARAM) {
                AstNamedNode* param_node = (AstNamedNode*)callee_entry_node;
                // If parameter doesn't have explicit function type, it's Item type
                if (!param_node->type || param_node->type->type_id != LMD_TYPE_FUNC) {
                    needs_item_extraction = true;
                }
            }
        }
        
        if (arg_count <= 3 && !is_variadic) {
            // Use specialized fn_callN for common cases (avoids list allocation)
            strbuf_append_format(tp->code_buf, "fn_call%d(", arg_count);
            
            // Emit function expression - Function* is a direct pointer like other containers
            if (primary_fn_node && primary_fn_node->expr && callee_entry_node) {
                if (needs_item_extraction) {
                    // Cast Item to Function* (direct pointer, no masking needed)
                    strbuf_append_str(tp->code_buf, "(Function*)");
                    write_var_name(tp->code_buf, (AstNamedNode*)callee_entry_node, NULL);
                } else {
                    write_var_name(tp->code_buf, (AstNamedNode*)callee_entry_node, NULL);
                }
            } else {
                strbuf_append_str(tp->code_buf, "(Function*)");
                transpile_expr(tp, call_node->function);
            }
            
            // Emit arguments
            arg = call_node->argument;
            while (arg) {
                strbuf_append_char(tp->code_buf, ',');
                transpile_box_item(tp, arg);
                arg = arg->next;
            }
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use fn_call with List for more arguments
            strbuf_append_str(tp->code_buf, "fn_call(");
            
            // Emit function expression - Function* is a direct pointer like other containers
            if (primary_fn_node && primary_fn_node->expr && callee_entry_node) {
                if (needs_item_extraction) {
                    // Cast Item to Function* (direct pointer, no masking needed)
                    strbuf_append_str(tp->code_buf, "(Function*)");
                    write_var_name(tp->code_buf, (AstNamedNode*)callee_entry_node, NULL);
                } else {
                    write_var_name(tp->code_buf, (AstNamedNode*)callee_entry_node, NULL);
                }
            } else {
                strbuf_append_str(tp->code_buf, "(Function*)");
                transpile_expr(tp, call_node->function);
            }
            
            // Build argument list
            strbuf_append_str(tp->code_buf, ",({Item _fa[]={");
            arg = call_node->argument;
            bool first = true;
            while (arg) {
                if (!first) strbuf_append_char(tp->code_buf, ',');
                transpile_box_item(tp, arg);
                arg = arg->next;
                first = false;
            }
            strbuf_append_format(tp->code_buf, 
                "}; List _fl={.type_id=%d,.items=_fa,.length=%d,.capacity=%d}; &_fl;}))",
                LMD_TYPE_LIST, arg_count, arg_count);
        }
        return;  // Done with function variable call
    }

    // Direct function call (original logic)
    // transpile the params
    strbuf_append_str(tp->code_buf, "(");
    bool has_output_arg = false;

    if (has_named_args && fn_node) {
        // named arguments: need to reorder arguments based on parameter names
        log_debug("handling named arguments");

        // create array to hold resolved arguments for each param position
        // using stack allocation for reasonable param counts
        #define MAX_PARAMS 32
        AstNode* resolved_args[MAX_PARAMS] = {0};
        
        // first pass: collect positional args and named args
        int positional_index = 0;
        arg = call_node->argument;
        while (arg) {
            if (arg->node_type == AST_NODE_NAMED_ARG) {
                // find the parameter by name
                AstNamedNode* named_arg = (AstNamedNode*)arg;
                AstNamedNode* param = find_param_by_name(fn_node, named_arg->name);
                if (param) {
                    // find the index of this param
                    int param_index = 0;
                    AstNamedNode* p = fn_node->param;
                    while (p && p != param) {
                        p = (AstNamedNode*)p->next;
                        param_index++;
                    }
                    if (param_index < MAX_PARAMS) {
                        if (resolved_args[param_index]) {
                            log_error("Error: duplicate argument for parameter '%s'", named_arg->name->chars);
                        }
                        resolved_args[param_index] = arg;
                        log_debug("named arg '%s' -> param index %d", named_arg->name->chars, param_index);
                    }
                } else {
                    log_error("Error: unknown parameter name '%s'", named_arg->name->chars);
                }
            } else {
                // positional argument
                if (positional_index < MAX_PARAMS && !resolved_args[positional_index]) {
                    resolved_args[positional_index] = arg;
                    log_debug("positional arg -> param index %d", positional_index);
                }
                positional_index++;
            }
            arg = arg->next;
        }

        // output arguments in parameter order
        TypeParam* param_type = fn_type ? fn_type->param : NULL;
        for (int i = 0; i < expected_count && i < MAX_PARAMS; i++) {
            if (has_output_arg) {
                strbuf_append_char(tp->code_buf, ',');
            }
            has_output_arg = true;

            AstNode* resolved_arg = resolved_args[i];
            transpile_call_argument(tp, resolved_arg, param_type, is_sys_func);
            param_type = param_type ? param_type->next : NULL;
        }

        // add variadic args list for variadic functions
        if (is_variadic) {
            if (has_output_arg) strbuf_append_char(tp->code_buf, ',');
            // collect any named args that exceed param_count into vargs
            strbuf_append_str(tp->code_buf, "null");  // TODO: named variadic args not supported yet
            has_output_arg = true;
        }
        #undef MAX_PARAMS
    }
    else {
        // no named arguments: simple positional processing
        arg = call_node->argument;
        TypeParam* param_type = fn_type ? fn_type->param : NULL;
        int arg_index = 0;

        // first pass: output regular parameters
        while (arg && (expected_count < 0 || arg_index < expected_count)) {
            if (has_output_arg) {
                strbuf_append_char(tp->code_buf, ',');
            }
            has_output_arg = true;

            transpile_call_argument(tp, arg, param_type, is_sys_func);

            arg = arg->next;
            param_type = param_type ? param_type->next : NULL;
            arg_index++;
        }

        // Fill missing arguments with default values or ITEM_NULL
        while (param_type) {
            log_debug("filling missing argument with default/null for param");
            if (has_output_arg) {
                strbuf_append_char(tp->code_buf, ',');
            }
            transpile_call_argument(tp, NULL, param_type, is_sys_func);
            has_output_arg = true;
            param_type = param_type->next;
        }

        // handle variadic arguments
        if (is_variadic) {
            if (has_output_arg) strbuf_append_char(tp->code_buf, ',');
            
            // count variadic args
            int varg_count = 0;
            AstNode* varg = arg;
            while (varg) { varg_count++; varg = varg->next; }
            
            if (varg_count == 0) {
                strbuf_append_str(tp->code_buf, "null");  // no variadic args
            } else {
                // build inline list: ({Item _va[]={...}; List _vl={...}; &_vl;})
                strbuf_append_str(tp->code_buf, "({Item _va[]={");
                bool first_varg = true;
                while (arg) {
                    if (!first_varg) strbuf_append_char(tp->code_buf, ',');
                    transpile_box_item(tp, arg);
                    arg = arg->next;
                    first_varg = false;
                }
                strbuf_append_format(tp->code_buf, 
                    "}; List _vl={.type_id=%d,.items=_va,.length=%d,.capacity=%d}; &_vl;})",
                    LMD_TYPE_LIST, varg_count, varg_count);
            }
            has_output_arg = true;
        } else {
            // discard extra arguments with warning (non-variadic case)
            while (arg) {
                log_warn("param_mismatch: discarding extra argument %d (function expects %d params)",
                    arg_index + 1, expected_count);
                arg = arg->next;
                arg_index++;
            }
        }
    }

    strbuf_append_char(tp->code_buf, ')');
}

void transpile_index_expr(Transpiler* tp, AstFieldNode *field_node) {
    // Defensive validation: ensure all required pointers and types are valid
    if (!field_node) {
        log_error("Error: transpile_index_expr called with null field_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    if (!field_node->object || !field_node->field) {
        log_error("Error: transpile_index_expr missing object or field");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    if (!field_node->object->type || !field_node->field->type) {
        log_error("Error: transpile_index_expr missing type information");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    
    TypeId object_type = field_node->object->type->type_id;
    TypeId field_type = field_node->field->type->type_id;
    
    // Check if field type is numeric (addressing the TODO comment)
    if (field_type != LMD_TYPE_INT && field_type != LMD_TYPE_INT64 && field_type != LMD_TYPE_FLOAT) {
        // Non-numeric index, must use generic fn_index
        strbuf_append_str(tp->code_buf, "fn_index(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    
    // Fast path optimizations for specific type combinations
    if (object_type == LMD_TYPE_ARRAY_INT && field_type == LMD_TYPE_INT) {
        // transpile_expr(tp, field_node->object);
        // strbuf_append_str(tp->code_buf, "->items[");
        // transpile_expr(tp, field_node->field);
        // strbuf_append_str(tp->code_buf, "]");
        // for safety, we have to call array_int_get
        strbuf_append_str(tp->code_buf, "array_int_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    else if (object_type == LMD_TYPE_ARRAY_INT64 && field_type == LMD_TYPE_INT) {
        // for safety, we have to call array_int64_get
        strbuf_append_str(tp->code_buf, "array_int64_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    else if (object_type == LMD_TYPE_ARRAY_FLOAT && field_type == LMD_TYPE_INT) {
        // for safety, we have to call array_float_get
        strbuf_append_str(tp->code_buf, "array_float_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }       
    else if (object_type == LMD_TYPE_ARRAY && field_type == LMD_TYPE_INT) {
        TypeArray* arr_type = (TypeArray*)field_node->object->type;
        if (arr_type->nested) {
            switch (arr_type->nested->type_id) {
                case LMD_TYPE_INT:
                    strbuf_append_str(tp->code_buf, "array_int_get(");  break;
                case LMD_TYPE_INT64:
                    strbuf_append_str(tp->code_buf, "array_int64_get(");  break;
                case LMD_TYPE_FLOAT:
                    strbuf_append_str(tp->code_buf, "array_float_get(");  break;
                default:
                    strbuf_append_str(tp->code_buf, "array_get(");
            }
        }
        else { 
            strbuf_append_str(tp->code_buf, "array_get(");
        }
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    else if (object_type == LMD_TYPE_LIST && field_type == LMD_TYPE_INT) {
        strbuf_append_str(tp->code_buf, "list_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }    
    else {
        // Generic fallback for all other cases - box both arguments
        strbuf_append_str(tp->code_buf, "fn_index(");
        transpile_box_item(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;    
    }
}

void transpile_member_expr(Transpiler* tp, AstFieldNode *field_node) {
    // defensive check: if object or field is null, emit error and skip
    if (!field_node->object || !field_node->field) {
        log_error("transpile_member_expr: null object or field");
        strbuf_append_str(tp->code_buf, "ItemError /* null member expr */");
        return;
    }
    if (!field_node->object->type) {
        log_error("transpile_member_expr: object missing type");
        strbuf_append_str(tp->code_buf, "ItemError /* missing type */");
        return;
    }
    
    if (field_node->object->type->type_id == LMD_TYPE_MAP) {
        strbuf_append_str(tp->code_buf, "map_get(");
        transpile_expr(tp, field_node->object);
    } 
    else if (field_node->object->type->type_id == LMD_TYPE_ELEMENT) {
        strbuf_append_str(tp->code_buf, "elmt_get(");
        transpile_expr(tp, field_node->object);
    }
    else {
        strbuf_append_str(tp->code_buf, "fn_member(");
        transpile_expr(tp, field_node->object);
    }
    strbuf_append_char(tp->code_buf, ',');
    if (field_node->field->node_type == AST_NODE_IDENT) {
        TSSymbol symbol = ts_node_symbol(field_node->field->node);
        TypeString* type = (TypeString*)build_lit_string(tp, field_node->field->node, symbol);
        strbuf_append_format(tp->code_buf, "const_s2it(%d)", type->const_index);
    } 
    else {
        transpile_box_item(tp, field_node->field);
    }
    strbuf_append_char(tp->code_buf, ')');    
}

// Emit a forward declaration for a function (just signature, no body)
void forward_declare_func(Transpiler* tp, AstFuncNode *fn_node) {
    bool is_closure = (fn_node->captures != nullptr);
    
    strbuf_append_char(tp->code_buf, '\n');
    // closures must return Item to be compatible with fn_call*
    if (is_closure) {
        strbuf_append_str(tp->code_buf, "Item");
    } else {
        TypeFunc* fn_type = (TypeFunc*)fn_node->type;
        Type *ret_type = fn_type->returned ? fn_type->returned : (fn_node->body ? fn_node->body->type : &TYPE_ANY);
        if (!ret_type) {
            ret_type = &TYPE_ANY;
        }
        write_type(tp->code_buf, ret_type);
    }
    strbuf_append_char(tp->code_buf, ' ');
    write_fn_name(tp->code_buf, fn_node, NULL);
    strbuf_append_char(tp->code_buf, '(');
    
    // for closures, add hidden env parameter as first param
    bool has_params = false;
    if (is_closure) {
        strbuf_append_str(tp->code_buf, "void* _env_ptr");
        has_params = true;
    }
    
    AstNamedNode *param = fn_node->param;
    while (param) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        TypeParam* param_type = (TypeParam*)param->type;
        // closures receive all params as Item from fn_call*
        if (is_closure || param_type->is_optional) {
            strbuf_append_str(tp->code_buf, "Item");
        } else {
            write_type(tp->code_buf, param->type);
        }
        strbuf_append_str(tp->code_buf, " _");
        strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
        param = (AstNamedNode*)param->next;
        has_params = true;
    }
    
    TypeFunc* fn_type = (TypeFunc*)fn_node->type;
    if (fn_type && fn_type->is_variadic) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        strbuf_append_str(tp->code_buf, "List* _vargs");
    }
    
    strbuf_append_str(tp->code_buf, ");\n");
}

void define_func(Transpiler* tp, AstFuncNode *fn_node, bool as_pointer) {
    bool is_closure = (fn_node->captures != nullptr);
    
    // Register function name mapping for stack traces (MIR name -> Lambda name)
    register_func_name(tp, fn_node);
    
    // Note: closure env struct is defined in pre_define_closure_envs()
    
    strbuf_append_char(tp->code_buf, '\n');
    // closures must return Item to be compatible with fn_call*
    Type *ret_type = ((TypeFunc*)fn_node->type)->returned;
    if (!ret_type && fn_node->body) {
        ret_type = fn_node->body->type;
    }
    if (!ret_type) {
        ret_type = &TYPE_ANY;
    }
    if (is_closure) {
        strbuf_append_str(tp->code_buf, "Item");
    } else {
        write_type(tp->code_buf, ret_type);
    }

    // write the function name, with a prefix '_', so as to diff from built-in functions
    strbuf_append_str(tp->code_buf, as_pointer ? " (*" :" ");
    write_fn_name(tp->code_buf, fn_node, NULL);
    if (as_pointer) strbuf_append_char(tp->code_buf, ')');

    // write the params
    strbuf_append_char(tp->code_buf, '(');
    
    // for closures, add hidden env parameter as first param
    bool has_params = false;
    if (is_closure) {
        strbuf_append_str(tp->code_buf, "void* _env_ptr");
        has_params = true;
    }
    
    AstNamedNode *param = fn_node->param;
    while (param) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        // closures receive all params as Item from fn_call*
        TypeParam* param_type = (TypeParam*)param->type;
        if (is_closure || param_type->is_optional) {
            strbuf_append_str(tp->code_buf, "Item");
        } else {
            write_type(tp->code_buf, param->type);
        }
        strbuf_append_str(tp->code_buf, " _");
        strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
        param = (AstNamedNode*)param->next;
        has_params = true;
    }

    // add hidden _vargs parameter for variadic functions
    TypeFunc* fn_type = (TypeFunc*)fn_node->type;
    if (fn_type && fn_type->is_variadic) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        strbuf_append_str(tp->code_buf, "List* _vargs");
    }

    if (as_pointer) {
        strbuf_append_str(tp->code_buf, ");\n");  return;
    }
    // write fn body
    strbuf_append_str(tp->code_buf, "){\n");
    
    // for closures, cast and extract captured variables from env
    if (is_closure) {
        strbuf_append_str(tp->code_buf, " ");
        write_env_name(tp->code_buf, fn_node);
        strbuf_append_str(tp->code_buf, "* _env = (");
        write_env_name(tp->code_buf, fn_node);
        strbuf_append_str(tp->code_buf, "*)_env_ptr;\n");
    }
    
    // Check if this function should use Tail Call Optimization
    // A function can only use TCO if ALL recursive calls are in tail position
    // (otherwise, transforming just the tail calls would break the function)
    bool use_tco = should_use_tco(fn_node) && is_tco_function_safe(fn_node);
    
    // Stack overflow check for potentially recursive functions
    // TCO functions don't need stack checks since they won't grow the stack
    bool needs_stack_check = !use_tco;
    
    if (needs_stack_check) {
        strbuf_append_str(tp->code_buf, " LAMBDA_STACK_CHECK(\"");
        // Use function name if available, otherwise use assignment name
        if (fn_node->name && fn_node->name->chars) {
            strbuf_append_str_n(tp->code_buf, fn_node->name->chars, fn_node->name->len);
        } else if (tp->current_assign_name && tp->current_assign_name->chars) {
            strbuf_append_str_n(tp->code_buf, tp->current_assign_name->chars, tp->current_assign_name->len);
        } else {
            strbuf_append_str(tp->code_buf, "<anonymous>");
        }
        strbuf_append_str(tp->code_buf, "\");\n");
    }
    
    // TCO: Add loop label at start of function body for goto target
    if (use_tco) {
        strbuf_append_str(tp->code_buf, " _tco_start:;\n");
    }
    
    // set vargs before function body for variadic functions
    if (fn_type && fn_type->is_variadic) {
        strbuf_append_str(tp->code_buf, " set_vargs(_vargs);\n");
    }
    
    // set current_closure context for body transpilation
    AstFuncNode* prev_closure = tp->current_closure;
    if (is_closure) {
        tp->current_closure = fn_node;
    }
    
    // TCO context setup
    AstFuncNode* prev_tco_func = tp->tco_func;
    bool prev_in_tail = tp->in_tail_position;
    if (use_tco) {
        tp->tco_func = fn_node;
        tp->in_tail_position = true;  // Function body is in tail position
    }
    
    // for procedures, use procedural content transpilation
    bool is_proc = (fn_node->node_type == AST_NODE_PROC);
    if (is_proc && fn_node->body->node_type == AST_NODE_CONTENT) {
        strbuf_append_str(tp->code_buf, " return ");
        transpile_proc_content(tp, (AstListNode*)fn_node->body);
        strbuf_append_str(tp->code_buf, ";\n}\n");
    } else {
        strbuf_append_str(tp->code_buf, " return ");
        // Check if we need to box the return value
        // Box if: (1) it's a closure, OR (2) return type is Item but body type is a scalar
        bool needs_boxing = is_closure;
        if (!needs_boxing && ret_type->type_id == LMD_TYPE_ANY && fn_node->body->type) {
            TypeId body_type_id = fn_node->body->type->type_id;
            // Box scalar types when returning as Item
            needs_boxing = (body_type_id == LMD_TYPE_INT || body_type_id == LMD_TYPE_INT64 ||
                           body_type_id == LMD_TYPE_FLOAT || body_type_id == LMD_TYPE_BOOL ||
                           body_type_id == LMD_TYPE_STRING || body_type_id == LMD_TYPE_SYMBOL ||
                           body_type_id == LMD_TYPE_BINARY || body_type_id == LMD_TYPE_DECIMAL ||
                           body_type_id == LMD_TYPE_DTIME);
        }
        
        if (needs_boxing) {
            transpile_box_item(tp, fn_node->body);
        } else {
            transpile_expr(tp, fn_node->body);
        }
        strbuf_append_str(tp->code_buf, ";\n}\n");
    }
    
    // Restore TCO context
    tp->tco_func = prev_tco_func;
    tp->in_tail_position = prev_in_tail;
    
    // restore previous closure context
    tp->current_closure = prev_closure;
}

// helper to generate boxing code for a captured variable
void transpile_box_capture(Transpiler* tp, CaptureInfo* cap, bool from_outer_env) {
    Type* type = cap->entry && cap->entry->node ? cap->entry->node->type : nullptr;
    TypeId type_id = type ? type->type_id : LMD_TYPE_ANY;
    
    if (from_outer_env) {
        // already boxed in outer env, just copy
        strbuf_append_str(tp->code_buf, "_env->");
        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
        return;
    }
    
    // box based on type
    switch (type_id) {
    case LMD_TYPE_INT:
        strbuf_append_str(tp->code_buf, "i2it(_");
        break;
    case LMD_TYPE_INT64:
        strbuf_append_str(tp->code_buf, "l2it(&_");
        break;
    case LMD_TYPE_FLOAT:
        strbuf_append_str(tp->code_buf, "d2it(&_");
        break;
    case LMD_TYPE_BOOL:
        strbuf_append_str(tp->code_buf, "b2it(_");
        break;
    case LMD_TYPE_STRING:
        strbuf_append_str(tp->code_buf, "s2it(_");
        break;
    case LMD_TYPE_SYMBOL:
        strbuf_append_str(tp->code_buf, "y2it(_");
        break;
    case LMD_TYPE_BINARY:
        strbuf_append_str(tp->code_buf, "x2it(_");
        break;
    case LMD_TYPE_DECIMAL:
        strbuf_append_str(tp->code_buf, "c2it(_");
        break;
    case LMD_TYPE_DTIME:
        strbuf_append_str(tp->code_buf, "k2it(&_");
        break;
    default:
        // for container types (List*, Map*, etc.) and Item, cast to Item
        strbuf_append_str(tp->code_buf, "(Item)_");
        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
        return;
    }
    strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_fn_expr(Transpiler* tp, AstFuncNode *fn_node) {
    TypeFunc* fn_type = (TypeFunc*)fn_node->type;
    int arity = fn_type ? fn_type->param_count : 0;
    
    // Register closure name mapping for stack traces (MIR name -> Lambda name)
    // This is done here because at this point we have access to current_assign_name
    register_func_name_with_context(tp, fn_node);
    
    if (fn_node->captures) {
        // closure: allocate env, populate captured values, call to_closure
        // generate: ({ Env_fXXX* env = heap_calloc(sizeof(Env_fXXX), 0); 
        //              env->var1 = i2it(_var1); env->var2 = s2it(_var2); ...
        //              to_closure_named(_fXXX, arity, env, "name"); })
        strbuf_append_str(tp->code_buf, "({ ");
        write_env_name(tp->code_buf, fn_node);
        strbuf_append_str(tp->code_buf, "* _closure_env = heap_calloc(sizeof(");
        write_env_name(tp->code_buf, fn_node);
        strbuf_append_str(tp->code_buf, "), 0);\n");
        
        // populate captured variables
        CaptureInfo* cap = fn_node->captures;
        while (cap) {
            strbuf_append_str(tp->code_buf, "  _closure_env->");
            strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
            strbuf_append_str(tp->code_buf, " = ");
            
            // box the captured variable to Item
            // the captured variable may be in our own closure env if we're a nested closure
            bool from_outer = false;
            if (tp->current_closure) {
                CaptureInfo* outer_cap = find_capture(tp->current_closure, cap->name);
                from_outer = (outer_cap != nullptr);
            }
            transpile_box_capture(tp, cap, from_outer);
            strbuf_append_str(tp->code_buf, ";\n");
            cap = cap->next;
        }
        
        strbuf_append_str(tp->code_buf, "  to_closure_named(");
        write_fn_name(tp->code_buf, fn_node, NULL);
        strbuf_append_format(tp->code_buf, ",%d,_closure_env,", arity);
        // pass function name as string literal for stack traces
        // prefer named function, fall back to assignment variable name, then <anonymous>
        if (fn_node->name && fn_node->name->chars) {
            strbuf_append_char(tp->code_buf, '"');
            strbuf_append_str_n(tp->code_buf, fn_node->name->chars, fn_node->name->len);
            strbuf_append_char(tp->code_buf, '"');
        } else if (tp->current_assign_name && tp->current_assign_name->chars) {
            strbuf_append_char(tp->code_buf, '"');
            strbuf_append_str_n(tp->code_buf, tp->current_assign_name->chars, tp->current_assign_name->len);
            strbuf_append_char(tp->code_buf, '"');
        } else {
            strbuf_append_str(tp->code_buf, "\"<anonymous>\"");
        }
        strbuf_append_str(tp->code_buf, "); })");
    } else {
        // regular function without captures - use to_fn_named for stack traces
        strbuf_append_format(tp->code_buf, "to_fn_named(");
        write_fn_name(tp->code_buf, fn_node, NULL);
        strbuf_append_format(tp->code_buf, ",%d,", arity);
        // pass function name as string literal for stack traces
        if (fn_node->name && fn_node->name->chars) {
            strbuf_append_char(tp->code_buf, '"');
            strbuf_append_str_n(tp->code_buf, fn_node->name->chars, fn_node->name->len);
            strbuf_append_char(tp->code_buf, '"');
        } else if (tp->current_assign_name && tp->current_assign_name->chars) {
            strbuf_append_char(tp->code_buf, '"');
            strbuf_append_str_n(tp->code_buf, tp->current_assign_name->chars, tp->current_assign_name->len);
            strbuf_append_char(tp->code_buf, '"');
        } else {
            strbuf_append_str(tp->code_buf, "\"<anonymous>\"");
        }
        strbuf_append_char(tp->code_buf, ')');
    }
}

void transpile_base_type(Transpiler* tp, AstTypeNode* type_node) {
    strbuf_append_format(tp->code_buf, "base_type(%d)", ((TypeType*)type_node->type)->type->type_id);
}

void transpile_binary_type(Transpiler* tp, AstBinaryNode* bin_node) {
    TypeBinary* binary_type = (TypeBinary*)((TypeType*)bin_node->type)->type;
    strbuf_append_format(tp->code_buf, "const_type(%d)", binary_type->type_index);
}

void transpile_expr(Transpiler* tp, AstNode *expr_node) {
    if (!expr_node) { log_error("missing expression node"); return; }
    // get the function name
    switch (expr_node->node_type) {
    case AST_NODE_PRIMARY:
        transpile_primary_expr(tp, (AstPrimaryNode*)expr_node);
        break;
    case AST_NODE_UNARY:
        transpile_unary_expr(tp, (AstUnaryNode*)expr_node);
        break;
    case AST_NODE_BINARY:
        transpile_binary_expr(tp, (AstBinaryNode*)expr_node);
        break;
    case AST_NODE_PIPE:
        transpile_pipe_expr(tp, (AstPipeNode*)expr_node);
        break;
    case AST_NODE_CURRENT_ITEM:
        // ~ references the current pipe context item
        strbuf_append_str(tp->code_buf, "_pipe_item");
        break;
    case AST_NODE_CURRENT_INDEX:
        // ~# references the current pipe context key/index
        strbuf_append_str(tp->code_buf, "_pipe_index");
        break;
    case AST_NODE_IF_EXPR:
        transpile_if(tp, (AstIfNode*)expr_node);
        break;
    case AST_NODE_IF_STAM:
        transpile_if(tp, (AstIfNode*)expr_node);
        break;
    case AST_NODE_FOR_EXPR:
        transpile_for(tp, (AstForNode*)expr_node);
        break;        
    case AST_NODE_FOR_STAM:
        transpile_for(tp, (AstForNode*)expr_node);
        break;
    case AST_NODE_WHILE_STAM:
        transpile_while(tp, (AstWhileNode*)expr_node);
        break;
    case AST_NODE_BREAK_STAM:
        strbuf_append_str(tp->code_buf, "break");
        break;
    case AST_NODE_CONTINUE_STAM:
        strbuf_append_str(tp->code_buf, "continue");
        break;
    case AST_NODE_RETURN_STAM:
        transpile_return(tp, (AstReturnNode*)expr_node);
        break;
    case AST_NODE_VAR_STAM:
        transpile_let_stam(tp, (AstLetNode*)expr_node, false);  // reuse let transpiler
        break;
    case AST_NODE_ASSIGN_STAM:
        transpile_assign_stam(tp, (AstAssignStamNode*)expr_node);
        break;
    case AST_NODE_ASSIGN:
        transpile_assign_expr(tp, (AstNamedNode*)expr_node);
        break;
    case AST_NODE_ARRAY:
        transpile_array_expr(tp, (AstArrayNode*)expr_node);
        break;
    case AST_NODE_LIST:
        transpile_list_expr(tp, (AstListNode*)expr_node);
        break;
    case AST_NODE_CONTENT:
        transpile_content_expr(tp, (AstListNode*)expr_node);
        break;
    case AST_NODE_MAP:
        transpile_map_expr(tp, (AstMapNode*)expr_node);
        break;
    case AST_NODE_ELEMENT:
        transpile_element(tp, (AstElementNode*)expr_node);
        break;
    case AST_NODE_MEMBER_EXPR:
        transpile_member_expr(tp, (AstFieldNode*)expr_node);
        break;
    case AST_NODE_INDEX_EXPR:
        transpile_index_expr(tp, (AstFieldNode*)expr_node);
        break;
    case AST_NODE_CALL_EXPR:
        transpile_call_expr(tp, (AstCallNode*)expr_node);
        break;
    case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM:  case AST_NODE_TYPE_STAM:
    case AST_NODE_FUNC:  case AST_NODE_PROC:
    case AST_NODE_STRING_PATTERN:  case AST_NODE_SYMBOL_PATTERN:
        // already transpiled or pattern definitions (handled at compile time)
        break;
    case AST_NODE_FUNC_EXPR:
        transpile_fn_expr(tp, (AstFuncNode*)expr_node);
        break;
    case AST_NODE_TYPE:
        transpile_base_type(tp, (AstTypeNode*)expr_node);
        break;
    case AST_NODE_LIST_TYPE: {
        TypeType* list_type = (TypeType*)((AstListNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeList*)list_type->type)->type_index);
        break;
    }
    case AST_NODE_ARRAY_TYPE: {
        TypeType* array_type = (TypeType*)((AstArrayNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeArray*)array_type->type)->type_index);
        break;
    }
    case AST_NODE_MAP_TYPE: {
        TypeType* map_type = (TypeType*)((AstMapNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeMap*)map_type->type)->type_index);
        break;
    }
    case AST_NODE_ELMT_TYPE: {
        TypeType* elmt_type = (TypeType*)((AstElementNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeElmt*)elmt_type->type)->type_index);
        break;
    }
    case AST_NODE_FUNC_TYPE: {
        TypeType* fn_type = (TypeType*)((AstFuncNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeFunc*)fn_type->type)->type_index);
        break;
    }
    case AST_NODE_BINARY_TYPE:
        transpile_binary_type(tp, (AstBinaryNode*)expr_node);
        break;
    case AST_NODE_IMPORT:
        log_debug("import module");
        break;
    default:
        log_debug("unknown expression type: %d!!!", expr_node->node_type);
        break;
    }
}

void define_module_import(Transpiler* tp, AstImportNode *import_node) {
    log_debug("define import module");
    // import module
    if (!import_node->script) { log_error("Error: missing script for import");  return; }
    log_debug("script reference: %s", import_node->script->reference);
    // loop through the public functions in the module
    AstNode *node = import_node->script->ast_root;
    if (!node) { log_error("Error: Missing root node in module_import");  return; }
    assert(node->node_type == AST_SCRIPT);
    node = ((AstScript*)node)->child;
    strbuf_append_format(tp->code_buf, "struct Mod%d {\n", import_node->script->index);
    while (node) {
        if (node->node_type == AST_NODE_CONTENT) {
            node = ((AstListNode*)node)->item;
            continue;
        } 
        else if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR || node->node_type == AST_NODE_PROC) {
            AstFuncNode *func_node = (AstFuncNode*)node;
            log_debug("got imported fn: %.*s, is_public: %d", (int)func_node->name->len, func_node->name->chars,
                ((TypeFunc*)func_node->type)->is_public);
            if (((TypeFunc*)func_node->type)->is_public) {
                define_func(tp, func_node, true);
            }
        } 
        else if (node->node_type == AST_NODE_PUB_STAM) {
            // let declaration
            AstNode *declare = ((AstLetNode*)node)->declare;
            while (declare) {
                AstNamedNode *asn_node = (AstNamedNode*)declare;
                // declare the type
                Type *type = asn_node->type;
                write_type(tp->code_buf, type);
                strbuf_append_char(tp->code_buf, ' ');
                write_var_name(tp->code_buf, asn_node, NULL);
                strbuf_append_str(tp->code_buf, ";\n");
                declare = declare->next;
            }
        }
        node = node->next;
    }
    strbuf_append_format(tp->code_buf, "} m%d;\n", import_node->script->index);
}

void define_ast_node(Transpiler* tp, AstNode *node) {
    // get the function name
    log_debug("define_ast_node: node %p, type %d", node, node ? node->node_type : -1);
    if (!node) return;
    switch(node->node_type) {
    case AST_NODE_IDENT:  case AST_NODE_PARAM:
        break;
    case AST_NODE_PRIMARY:
        if (((AstPrimaryNode*)node)->expr) {
            define_ast_node(tp, ((AstPrimaryNode*)node)->expr);
        }
        break;
    case AST_NODE_UNARY:
        define_ast_node(tp, ((AstUnaryNode*)node)->operand);
        break;
    case AST_NODE_BINARY:
        define_ast_node(tp, ((AstBinaryNode*)node)->left);
        define_ast_node(tp, ((AstBinaryNode*)node)->right);
        break;
    case AST_NODE_IF_EXPR: {
        AstIfNode* if_node = (AstIfNode*)node;
        define_ast_node(tp, if_node->cond);
        define_ast_node(tp, if_node->then);
        if (if_node->otherwise) {
            define_ast_node(tp, if_node->otherwise);
        }
        break;
    }
    case AST_NODE_IF_STAM: {
        AstIfNode* if_node = (AstIfNode*)node;
        define_ast_node(tp, if_node->cond);
        define_ast_node(tp, if_node->then);
        if (if_node->otherwise) {
            define_ast_node(tp, if_node->otherwise);
        }
        break;
    }
    case AST_NODE_LET_STAM:  case AST_NODE_TYPE_STAM: {
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            define_ast_node(tp, declare);
            declare = declare->next;
        }
        break;
    }
    case AST_NODE_STRING_PATTERN:  case AST_NODE_SYMBOL_PATTERN: {
        // Pattern definitions - compile the pattern and store in type_list
        AstPatternDefNode* pattern_def = (AstPatternDefNode*)node;
        TypePattern* pattern_type = (TypePattern*)pattern_def->type;
        
        // Compile pattern to regex if not already compiled
        if (pattern_type->re2 == nullptr && pattern_def->as != nullptr) {
            const char* error_msg = nullptr;
            TypePattern* compiled = compile_pattern_ast(tp->pool, pattern_def->as, 
                pattern_def->is_symbol, &error_msg);
            if (compiled) {
                // Copy compiled info to existing type
                pattern_type->re2 = compiled->re2;
                pattern_type->source = compiled->source;
                // Add to type_list for runtime access
                arraylist_append(tp->type_list, pattern_type);
                pattern_type->pattern_index = tp->type_list->length - 1;
                log_debug("compiled pattern '%.*s' to regex, index=%d", 
                    (int)pattern_def->name->len, pattern_def->name->chars, pattern_type->pattern_index);
            } else {
                log_error("failed to compile pattern '%.*s': %s",
                    (int)pattern_def->name->len, pattern_def->name->chars, 
                    error_msg ? error_msg : "unknown error");
            }
        }
        break;
    }
    case AST_NODE_PUB_STAM: {
        // pub vars need to be brought to global scope in C, to allow them to be exported
        AstNode *decl = ((AstLetNode*)node)->declare;
        while (decl) {
            transpile_assign_expr(tp, (AstNamedNode*)decl);
            decl = decl->next;
        }
        break;
    }
    case AST_NODE_FOR_EXPR: {
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            define_ast_node(tp, loop);
            loop = loop->next;
        }
        define_ast_node(tp, ((AstForNode*)node)->then);
        break;
    }
    case AST_NODE_FOR_STAM: {
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            define_ast_node(tp, loop);
            loop = loop->next;
        }
        define_ast_node(tp, ((AstForNode*)node)->then);
        break;
    }
    case AST_NODE_WHILE_STAM: {
        AstWhileNode* while_node = (AstWhileNode*)node;
        define_ast_node(tp, while_node->cond);
        define_ast_node(tp, while_node->body);
        break;
    }
    case AST_NODE_BREAK_STAM:
    case AST_NODE_CONTINUE_STAM:
        // no child nodes to define
        break;
    case AST_NODE_RETURN_STAM: {
        AstReturnNode* ret_node = (AstReturnNode*)node;
        if (ret_node->value) {
            define_ast_node(tp, ret_node->value);
        }
        break;
    }
    case AST_NODE_VAR_STAM: {
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            define_ast_node(tp, declare);
            declare = declare->next;
        }
        break;
    }
    case AST_NODE_ASSIGN_STAM: {
        AstAssignStamNode* assign = (AstAssignStamNode*)node;
        define_ast_node(tp, assign->value);
        break;
    }
    case AST_NODE_ASSIGN: {
        AstNamedNode* assign = (AstNamedNode*)node;
        define_ast_node(tp, assign->as);
        break;
    }
    case AST_NODE_KEY_EXPR: {
        AstNamedNode* key = (AstNamedNode*)node;
        define_ast_node(tp, key->as);
        break;
    }
    case AST_NODE_LOOP:
        define_ast_node(tp, ((AstNamedNode*)node)->as);
        break;
    case AST_NODE_ARRAY: {
        AstNode *item = ((AstArrayNode*)node)->item;
        while (item) {
            define_ast_node(tp, item);
            item = item->next;
        }        
        break;
    }
    case AST_NODE_LIST:  case AST_NODE_CONTENT: {
        AstNode *ld = ((AstListNode*)node)->declare;
        while (ld) {
            define_ast_node(tp, ld);
            ld = ld->next;
        }        
        AstNode *li = ((AstListNode*)node)->item;
        while (li) {
            define_ast_node(tp, li);
            li = li->next;
        }        
        break; 
    }
    case AST_NODE_MAP:  case AST_NODE_ELEMENT: {
        AstNode *nm_item = ((AstMapNode*)node)->item;
        while (nm_item) {
            define_ast_node(tp, nm_item);
            nm_item = nm_item->next;
        }
        break;
    }
    case AST_NODE_MEMBER_EXPR:  case AST_NODE_INDEX_EXPR:
        define_ast_node(tp, ((AstFieldNode*)node)->object);
        define_ast_node(tp, ((AstFieldNode*)node)->field);
        break;
    case AST_NODE_CALL_EXPR: {
        define_ast_node(tp, ((AstCallNode*)node)->function);
        AstNode* arg = ((AstCallNode*)node)->argument;
        while (arg) {
            define_ast_node(tp, arg);
            arg = arg->next;
        }
        break;
    }
    case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR:  case AST_NODE_PROC: {
        // func needs to be brought to global scope in C
        define_func(tp, (AstFuncNode*)node, false);
        AstFuncNode* func = (AstFuncNode*)node;
        AstNode* fn_param = (AstNode*)func->param;
        while (fn_param) {
            define_ast_node(tp, fn_param);
            fn_param = fn_param->next;
        }        
        define_ast_node(tp, func->body);
        break;
    }
    case AST_NODE_IMPORT:
        define_module_import(tp, (AstImportNode*)node);
        break;
    case AST_NODE_SYS_FUNC:
        // todo: should define its params
        break;
    case AST_NODE_TYPE:  case AST_NODE_LIST_TYPE:  case AST_NODE_ARRAY_TYPE:
    case AST_NODE_MAP_TYPE:  case AST_NODE_ELMT_TYPE:  case AST_NODE_BINARY_TYPE:
        // nothing to define at the moment
        break;
    default:
        log_debug("unknown expression type: %d", node->node_type);
        break;
    }
}

void declare_global_var(Transpiler* tp, AstLetNode *let_node) {
    // declare global vars
    AstNode *decl = let_node->declare;
    while (decl) {
        if (decl->node_type == AST_NODE_DECOMPOSE) {
            // Handle decomposition - declare all variables as Item
            AstDecomposeNode* dec_node = (AstDecomposeNode*)decl;
            for (int i = 0; i < dec_node->name_count; i++) {
                String* name = dec_node->names[i];
                strbuf_append_str(tp->code_buf, "Item _");
                strbuf_append_str_n(tp->code_buf, name->chars, name->len);
                strbuf_append_str(tp->code_buf, ";\n");
            }
        } else {
            AstNamedNode *asn_node = (AstNamedNode*)decl;
            Type *var_type = asn_node->type;
            write_type(tp->code_buf, var_type);
            strbuf_append_char(tp->code_buf, ' ');
            write_var_name(tp->code_buf, asn_node, NULL);
            strbuf_append_str(tp->code_buf, ";\n");
        }
        decl = decl->next;
    }
}

void assign_global_var(Transpiler* tp, AstLetNode *let_node) {
    // declare global vars
    AstNode *decl = let_node->declare;
    while (decl) {
        if (decl->node_type == AST_NODE_DECOMPOSE) {
            // Handle decomposition at global level using a nested scope
            AstDecomposeNode* dec_node = (AstDecomposeNode*)decl;
            strbuf_append_str(tp->code_buf, "\n {Item _dec_src=");
            transpile_box_item(tp, dec_node->as);
            strbuf_append_str(tp->code_buf, ";");
            
            // Decompose into individual variables (global - no type declaration)
            for (int i = 0; i < dec_node->name_count; i++) {
                String* name = dec_node->names[i];
                strbuf_append_str(tp->code_buf, "\n  _");
                strbuf_append_str_n(tp->code_buf, name->chars, name->len);
                strbuf_append_char(tp->code_buf, '=');
                
                if (dec_node->is_named) {
                    strbuf_append_str(tp->code_buf, "item_attr(_dec_src,\"");
                    strbuf_append_str_n(tp->code_buf, name->chars, name->len);
                    strbuf_append_str(tp->code_buf, "\");");
                } else {
                    strbuf_append_format(tp->code_buf, "item_at(_dec_src,%d);", i);
                }
            }
            strbuf_append_str(tp->code_buf, "}");  // close nested scope
        } else {
            AstNamedNode *asn_node = (AstNamedNode*)decl;
            strbuf_append_str(tp->code_buf, "\n ");
            strbuf_append_char(tp->code_buf, ' ');
            write_var_name(tp->code_buf, asn_node, NULL);
            strbuf_append_char(tp->code_buf, '=');
            transpile_expr(tp, asn_node->as);
            strbuf_append_char(tp->code_buf, ';');
        }
        decl = decl->next;
    }
}

// include lambda-embed.h to get the lambda header file content as a string
#include "lambda-embed.h"

// Stack overflow check code to be included in transpiled output
// Uses portable approach compatible with MIR C compiler
// Access stack_limit directly from runtime context (rt->stack_limit) for speed
static const char* stack_check_code = R"(
// Stack overflow protection declarations
extern void lambda_stack_overflow_error(const char* func_name);

// Fast stack check using local variable address and context's cached stack_limit
#define LAMBDA_STACK_CHECK(func_name) \
    { \
        volatile char _stack_marker; \
        if ((uintptr_t)&_stack_marker < rt->stack_limit) { \
            lambda_stack_overflow_error(func_name); \
            return ITEM_ERROR; \
        } \
    }
)";

void transpile_ast_root(Transpiler* tp, AstScript *script) {
    strbuf_append_str_n(tp->code_buf, (const char*)lambda_lambda_h, lambda_lambda_h_len);
    // Add stack overflow protection code
    strbuf_append_str(tp->code_buf, stack_check_code);
    // all (nested) function definitions need to be hoisted to global level
    log_debug("define_ast_node ...");
    // Import shared runtime context pointer from native runtime
    // This ensures all modules (main + imports) share the same rt
    // _lambda_rt is a Context* in the native runtime, and its address is exported
    // MIR import resolves to the address of _lambda_rt, so we declare it as Context*
    strbuf_append_str(tp->code_buf, "\nextern Context* _lambda_rt;\n");
    strbuf_append_str(tp->code_buf, "#define rt _lambda_rt\n");
    
    // Pre-define all closure environment structs before any function definitions
    // This ensures structs are available when functions reference them
    AstNode* child = script->child;
    while (child) {
        if (child->node_type == AST_NODE_CONTENT) {
            AstNode* item = ((AstListNode*)child)->item;
            while (item) {
                pre_define_closure_envs(tp, item);
                item = item->next;
            }
        } else {
            pre_define_closure_envs(tp, child);
        }
        child = child->next;
    }

    // Forward declare all top-level functions to support out-of-order definitions
    child = script->child;
    while (child) {
        if (child->node_type == AST_NODE_CONTENT) {
            AstNode* item = ((AstListNode*)child)->item;
            while (item) {
                if (item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_PROC) {
                    forward_declare_func(tp, (AstFuncNode*)item);
                }
                item = item->next;
            }
            child = child->next;
        } else if (child->node_type == AST_NODE_FUNC || child->node_type == AST_NODE_PROC) {
            forward_declare_func(tp, (AstFuncNode*)child);
            child = child->next;
        } else {
            child = child->next;
        }
    }

    // declare global vars, types, define fns, etc.
    child = script->child;
    while (child) {
        switch (child->node_type) {
        case AST_NODE_CONTENT:
            child = ((AstListNode*)child)->item;
            continue;  // restart the loop with the first content item
        // declare global vars, types
        case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM:  case AST_NODE_TYPE_STAM:
            declare_global_var(tp, (AstLetNode*)child);
        default:
            define_ast_node(tp, child);
        }
        child = child->next;
    }

    // global evaluation, wrapped inside main()
    log_debug("transpile main() ...");
    strbuf_append_str(tp->code_buf, "\nItem main(Context *runtime) {\n _lambda_rt = runtime;\n");

    // transpile body content
    strbuf_append_str(tp->code_buf, " Item result = ({");
    child = script->child;
    bool has_content = false;
    while (child) {
        switch (child->node_type) {
        case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM:  case AST_NODE_TYPE_STAM:
            assign_global_var(tp, (AstLetNode*)child);       
            break;  // already handled
        case AST_NODE_IMPORT:  case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR:  case AST_NODE_PROC:
        case AST_NODE_STRING_PATTERN:  case AST_NODE_SYMBOL_PATTERN:
            break;  // skip global definition nodes
        case AST_NODE_CONTENT:
            transpile_content_expr(tp, (AstListNode*)child, true);
            has_content = true;
            break;  // already handled
        default:
            // AST_NODE_PRIMARY, AST_NODE_BINARY, etc.
            log_debug("transpile main(): boxing child, node_type=%d, type=%d", child->node_type, child->type ? child->type->type_id : -1);
            transpile_box_item(tp, child);
            has_content = true;            
        }
        child = child->next;
    }    
    if (!has_content) { strbuf_append_str(tp->code_buf, "ITEM_NULL"); }
    strbuf_append_str(tp->code_buf, ";});\n");

    // transpile invocation of main procedure if defined
    log_debug("transpiling main proc (if any)...");
    child = script->child;
    bool has_main = false;
    while (child) {
        switch (child->node_type) {
        case AST_NODE_PROC: {
            AstFuncNode* proc_node = (AstFuncNode*)child;
            log_debug("got global proc: %.*s", (int)proc_node->name->len, proc_node->name->chars);
            if (strcmp(proc_node->name->chars, "main") == 0) {
                strbuf_append_str(tp->code_buf, " if (rt->run_main) result = ");
                write_fn_name(tp->code_buf, proc_node, NULL);
                // todo: pass command line args
                strbuf_append_str(tp->code_buf, "();\n");
                has_main = true;
            }
            break;
        }
        case AST_NODE_CONTENT: {
            child = ((AstListNode*)child)->item;
            continue;  // restart the loop with the first content item
        }
        default:
            log_debug("not a proc: %d", child->node_type);
            break;  // skip other nodes
        }
        child = child->next;
    }
    log_debug("done transpiling main proc, has_main: %d", has_main);

    // return the result
    strbuf_append_str(tp->code_buf, " return result;\n}\n");
}
