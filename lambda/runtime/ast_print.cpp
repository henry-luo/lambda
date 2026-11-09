#include "ast.hpp"
#include "lambda/core/print.h"
#include "lambda/core/lambda-decimal.hpp"
#include "lib/log.h"
#include <inttypes.h>

#if !defined(LAMBDA_PRINT_VALUE_ONLY) && !defined(LAMBDA_NO_CONSOLE_DUMP)
void print_label(int indent, const char *label) {
    log_debug("  %s", label);
}

void print_const(Script *script, Type* type) {
    const char* type_name = type_info[type->type_id].name;
    if (type->type_id == LMD_TYPE_NULL || type->type_id == LMD_TYPE_BOOL || type->type_id == LMD_TYPE_INT
        || type->type_id == LMD_TYPE_NUM_SIZED) {
        log_debug("[const: %s]", type_name);  return;
    }
    TypeConst *const_type = (TypeConst*)type;
    void* data = script->const_list->data[const_type->const_index];
    switch (type->type_id) {
    case LMD_TYPE_FLOAT: {
        double num = *(double*)data;
        log_debug("[const@%d, %s, %g]", const_type->const_index, type_name, num);
        break;
    }
    case LMD_TYPE_INT64: {
        int64_t num = *(int64_t*)data;
        log_debug("[const@%d, %s, %" PRId64 "]", const_type->const_index, type_name, num);
        break;
    }
    case LMD_TYPE_UINT64: {
        uint64_t num = *(uint64_t*)data;
        log_debug("[const@%d, %s, %" PRIu64 "]", const_type->const_index, type_name, num);
        break;
    }
    case LMD_TYPE_DTIME: {
        DateTime datetime = *(DateTime*)data;
        StrBuf *strbuf = strbuf_new();
        datetime_format_lambda(strbuf, &datetime);
        log_debug("[const@%d, %s, '%s']", const_type->const_index, type_name, strbuf->str);
        strbuf_free(strbuf);
        break;
    }
    case LMD_TYPE_STRING: {
        String* string = (String*)data;
        log_debug("[const@%d, %s, %p, '%.*s']", const_type->const_index,
            type_name, string, (int)string->len, string->chars);
        break;
    }
    case LMD_TYPE_BINARY: {
        Binary* binary = (Binary*)data;
        StrBuf* literal = strbuf_new();
        // binary constants may contain NUL, so diagnostics must remain length-based too.
        format_binary_literal(literal, binary);
        log_debug("[const@%d, %s, %p, %s]", const_type->const_index,
            type_name, binary, literal->str);
        strbuf_free(literal);
        break;
    }
    case LMD_TYPE_SYMBOL: {
        Symbol* symbol = (Symbol*)data;
        log_debug("[const@%d, %s, %p, '%.*s']", const_type->const_index,
            type_name, symbol, (int)symbol->len, symbol->chars);
        break;
    }
    case LMD_TYPE_DECIMAL: {
        Decimal *decimal = (Decimal*)data;
        StrBuf *strbuf = strbuf_new();
        print_decimal(strbuf, decimal);
        log_debug("[const@%d, %s, %s]", const_type->const_index, type_name, strbuf->str);
        strbuf_free(strbuf);
        break;
    }
    default:  // LMD_TYPE_BOOL, LMD_TYPE_INT should not be in const pool
        log_debug("[const: %s, unexpected!!]", type_name);
    }
}

void print_ast_node(Script *script, AstNode *node, int indent) {
    if (!script) {
        log_debug("[null script]");  return;
    }
    if (!node) {
        log_debug("[null node]");  return;
    }
    if (indent > 0) log_enter();
    const char* type_name = node->type ? type_info[node->type->type_id].name : "unknown";
    // log_debug("print_ast_node: node_type=%d, name=%s", node->node_type, type_name);
    switch(node->node_type) {
    case AST_NODE_IDENT:
        log_debug("[ident:%.*s:%s,const:%d]", (int)((AstIdentNode*)node)->name->len,
            ((AstIdentNode*)node)->name->chars, type_name, node->type ? node->type->is_const : -1);
        break;
    case AST_NODE_PRIMARY:
        log_debug("[primary expr:%s,const:%d]", type_name, node->type ? node->type->is_const : -1);
        if (((AstPrimaryNode*)node)->expr) {
            print_ast_node(script, ((AstPrimaryNode*)node)->expr, indent + 1);
        } else {
            // for (int i = 0; i < indent+1; i++) { log_debug("  "); }
            log_enter();
            if (node->type && node->type->is_const) {
                print_const(script, node->type);
            }
            else {
                SourceSpan span = node->source_span;
                int length = (int)lambda_source_span_length(span);
                log_debug("(%.*s)", length, script->source + span.start_byte);
            }
            log_leave();
        }
        break;
    case AST_NODE_UNARY:
    case AST_NODE_SPREAD:
        log_debug("[unary expr %.*s:%s]", (int)((AstUnaryNode*)node)->op_str.length,
            ((AstUnaryNode*)node)->op_str.str, type_name);
        print_ast_node(script, ((AstUnaryNode*)node)->operand, indent + 1);
        break;
    case AST_NODE_BINARY: {
        AstBinaryNode* bnode = (AstBinaryNode*)node;
        log_debug("[binary expr %.*s.%d:%s]", (int)bnode->op_str.length, bnode->op_str.str,
            bnode->op, type_name);
        print_ast_node(script, bnode->left, indent + 1);
        print_ast_node(script, bnode->right, indent + 1);
        break;
    }
    case AST_NODE_IF_EXPR: {
        log_debug("[if:%s]", type_name);
        AstIfNode* if_node = (AstIfNode*)node;
        print_ast_node(script, if_node->cond, indent + 1);
        print_label(indent + 1, "then:");
        print_ast_node(script, if_node->then, indent + 1);
        if (if_node->otherwise) {
            print_label(indent + 1, "else:");
            print_ast_node(script, if_node->otherwise, indent + 1);
        }
        break;
    }
    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match = (AstMatchNode*)node;
        log_debug("[match expr:%s] arms=%d", type_name, match->arm_count);
        print_label(indent + 1, "scrutinee:");
        print_ast_node(script, match->scrutinee, indent + 1);
        AstMatchArm* arm = match->first_arm;
        while (arm) {
            if (arm->pattern) {
                print_label(indent + 1, "pattern:");
                print_ast_node(script, arm->pattern, indent + 2);
            } else {
                print_label(indent + 1, "default:");
            }
            print_label(indent + 1, "body:");
            print_ast_node(script, arm->body, indent + 2);
            arm = (AstMatchArm*)arm->next;
        }
        break;
    }
    case AST_NODE_TYPE_STAM: {
        log_debug("[type def:%s]", type_name);
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            print_label(indent + 1, "declare:");
            print_ast_node(script, declare, indent + 1);
            declare = declare->next;
        }
        break;
    }
    case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM: {
        log_debug("[%s stam:%s]", node->node_type == AST_NODE_PUB_STAM ? "pub" : "let", type_name);
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            print_label(indent + 1, "declare:");
            print_ast_node(script, declare, indent + 1);
            declare = declare->next;
        }
        break;
    }
    case AST_NODE_FOR_EXPR: {
        log_debug("[for %s:%s]",
            node->node_type == AST_NODE_FOR_EXPR ? "expr" : "stam", type_name);
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            print_label(indent + 1, "loop:");
            print_ast_node(script, loop, indent + 1);
            loop = loop->next;
        }
        print_label(indent + 1, "then:");
        print_ast_node(script, ((AstForNode*)node)->then, indent + 1);
        break;
    }
    case AST_NODE_ASSIGN: {
        AstNamedNode* assign = (AstNamedNode*)node;
        log_debug("[assign expr:%.*s:%s]", (int)assign->name->len, assign->name->chars, type_name);
        print_ast_node(script, assign->as, indent + 1);
        break;
    }
    case AST_NODE_KEY_EXPR: {
        AstNamedNode* key = (AstNamedNode*)node;
        if (key->key) {
            log_debug("[key expr:computed:%s]", type_name);
            print_ast_node(script, key->key, indent + 1);
        } else {
            log_debug("[key expr:%.*s:%s]", key->name ? (int)key->name->len : 0,
                key->name ? key->name->chars : "", type_name);
        }
        print_ast_node(script, key->as, indent + 1);
        break;
    }
    case AST_NODE_FOR_CLAUSE:
        log_debug("[loop expr:%s]", type_name);
        print_ast_node(script, ((AstLoopNode*)node)->as, indent + 1);
        break;
    case AST_NODE_ARRAY: {
        log_debug("[array expr:%s]", type_name);
        AstNode *item = ((AstArrayNode*)node)->item;
        while (item) {
            print_label(indent + 1, "item:");
            print_ast_node(script, item, indent + 1);
            item = item->next;
        }
        break;
    }
    case AST_NODE_LIST:  case AST_NODE_CONTENT:  case AST_NODE_CONTENT_TYPE: {
        AstListNode* list_node = (AstListNode*)node;
        log_debug("[%s:%s[%ld]]",
            node->node_type == AST_NODE_CONTENT_TYPE ? "content_type" :
            node->node_type == AST_NODE_CONTENT ? "content" : "list",
            type_name, list_node->list_type->length);
        AstNode *ld = list_node->declare;
        if (!ld) {
            print_label(indent + 1, "no declare");
        }
        while (ld) {
            print_label(indent + 1, "declare:");
            print_ast_node(script, ld, indent + 1);
            ld = ld->next;
        }
        AstNode *li = list_node->item;
        while (li) {
            print_label(indent + 1, "item:");
            print_ast_node(script, li, indent + 1);
            li = li->next;
        }
        break;
    }
    case AST_NODE_MAP: {
        log_debug("[map expr:%s]", type_name);
        AstNode *nm_item = ((AstMapNode*)node)->item;
        while (nm_item) {
            print_label(indent + 1, "map item:");
            print_ast_node(script, nm_item, indent + 1);
            nm_item = nm_item->next;
        }
        break;
    }
    case AST_NODE_ELEMENT: {
        log_debug("[elmt expr:%s]", type_name);
        AstElementNode* elmt_node = (AstElementNode*)node;
        AstNode *elmt_item = elmt_node->item;
        while (elmt_item) {
            print_label(indent + 1, "attr:");
            print_ast_node(script, elmt_item, indent + 1);
            elmt_item = elmt_item->next;
        }
        if (elmt_node->content) print_ast_node(script, elmt_node->content, indent + 1);
        break;
    }
    case AST_NODE_PARAM: {
        AstNamedNode* param = (AstNamedNode*)node;
        log_debug("[param: %.*s:%s]", (int)param->name->len, param->name->chars, type_name);
        break;
    }
    case AST_NODE_MEMBER_EXPR:  case AST_NODE_INDEX_EXPR:
        log_debug("[%s expr:%s]", node->node_type == AST_NODE_MEMBER_EXPR ? "member" : "index", type_name);
        print_label(indent + 1, "object:");
        print_ast_node(script, ((AstFieldNode*)node)->object, indent + 1);
        print_label(indent + 1, "field:");
        print_ast_node(script, ((AstFieldNode*)node)->field, indent + 1);
        break;
    case AST_NODE_CALL_EXPR: {
        Type *type = node->type;
        log_debug("[call expr:%s,const:%d]", type_name, type->is_const);
        print_ast_node(script, ((AstCallNode*)node)->function, indent + 1);
        print_label(indent + 1, "args:");
        AstNode* arg = ((AstCallNode*)node)->argument;
        while (arg) {
            log_debug("  (arg:%s)", arg->type ? type_info[arg->type->type_id].name : "unknown");
            print_ast_node(script, arg, indent + 1);
            arg = arg->next;
        }
        break;
    }
    case AST_NODE_SYS_FUNC: {
        AstSysFuncNode* sys_node = (AstSysFuncNode*)node;
        log_debug("[sys %s_%s:%s]", sys_node->fn_info->is_proc ? "pn" : "fn", sys_node->fn_info->name, type_name);
        break;
    }
    case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR: case AST_NODE_PROC: {
        // function definition
        AstFuncNode* func = (AstFuncNode*)node;
        if (node->node_type == AST_NODE_FUNC_EXPR) {
            log_debug("[fn expr:%s]", type_name);
        }
        else if (node->node_type == AST_NODE_FUNC) {
            log_debug("[fn: %.*s:%s]", (int)func->name->len, func->name->chars, type_name);
        }
        else {
            log_debug("[pn: %.*s:%s]", (int)func->name->len, func->name->chars, type_name);
        }
        print_label(indent + 1, "params:");
        AstNode* fn_param = (AstNode*)func->param;
        while (fn_param) {
            print_ast_node(script, fn_param, indent + 1);
            fn_param = fn_param->next;
        }
        print_ast_node(script, func->body, indent + 1);
        break;
    }
    case AST_NODE_TYPE: {
        TypeType* actual_type = (TypeType*)node->type;
        assert(node->type->type_id == LMD_TYPE_TYPE && actual_type->type);
        const char* actual_type_name = type_info[actual_type->type->type_id].name;
        log_debug("[%s: %s]", type_name, actual_type_name);
        break;
    }
    case AST_NODE_LIST_TYPE: {
        log_debug("[list type:%s]", type_name);
        AstNode *ls_item = ((AstListNode*)node)->item;
        while (ls_item) {
            print_label(indent + 1, "item:");
            print_ast_node(script, ls_item, indent + 1);
            ls_item = ls_item->next;
        }
        break;
    }
    case AST_NODE_ARRAY_TYPE: {
        log_debug("[array type:%s]", type_name);
        AstNode *arr_item = ((AstArrayNode*)node)->item;
        while (arr_item) {
            print_label(indent + 1, "item:");
            print_ast_node(script, arr_item, indent + 1);
            arr_item = arr_item->next;
        }
        break;
    }
    case AST_NODE_MAP_TYPE: {
        log_debug("[map type:%s]", type_name);
        AstNode *mt_item = ((AstMapNode*)node)->item;
        while (mt_item) {
            print_label(indent + 1, "map item:");
            print_ast_node(script, mt_item, indent + 1);
            mt_item = mt_item->next;
        }
        break;
    }
    case AST_NODE_ELMT_TYPE: {
        log_debug("[elmt type:%s]", type_name);
        AstElementNode* et_node = (AstElementNode*)node;
        AstNode *et_item = et_node->item;
        while (et_item) {
            print_label(indent + 1, "attr:");
            print_ast_node(script, et_item, indent + 1);
            et_item = et_item->next;
        }
        if (et_node->content) print_ast_node(script, et_node->content, indent + 1);
        break;
    }
    case AST_NODE_FUNC_TYPE: {
        log_debug("[func type:%s]", type_name);
        AstFuncNode* ft = (AstFuncNode*)node;
        print_label(indent + 1, "params:");
        AstNode* ft_param = (AstNode*)ft->param;
        while (ft_param) {
            print_ast_node(script, ft_param, indent + 1);
            ft_param = ft_param->next;
        }
        break;
    }
    case AST_NODE_BINARY_TYPE: {
        AstBinaryNode* bt_node = (AstBinaryNode*)node;
        log_debug("[binary type %.*s.%d:%s]", (int)bt_node->op_str.length, bt_node->op_str.str,
            bt_node->op, type_name);
        print_ast_node(script, bt_node->left, indent + 1);
        print_ast_node(script, bt_node->right, indent + 1);
        break;
    }
    case AST_NODE_UNARY_TYPE: {
        AstUnaryNode* ut_node = (AstUnaryNode*)node;
        log_debug("[unary type %.*s.%d:%s]", (int)ut_node->op_str.length, ut_node->op_str.str,
            ut_node->op, type_name);
        print_ast_node(script, ut_node->operand, indent + 1);
        break;
    }
    case AST_NODE_IMPORT: {
        AstImportNode* import_node = (AstImportNode*)node;
        if (!import_node->module.str) {
            log_debug("[import: missing module!!]");
        } else {
            log_debug("[import %.*s%s%.*s]",
                (int)import_node->module.length, import_node->module.str,
                (import_node->alias ? ":" : ""),
                (int)(import_node->alias ? import_node->alias->len:0), (import_node->alias ? import_node->alias->chars : ""));
        }
        break;
    }
    case AST_SCRIPT: {
        log_debug("[script:%s]", type_name);
        AstNode* child = ((AstScript*)node)->child;
        while (child) {
            print_ast_node(script, child, indent + 1);
            child = child->next;
        }
        break;
    }
    default:
        log_debug("[unknown expression type: %d!]", node->node_type);
        break;
    }
    if (indent > 0) log_leave();
}

#endif
