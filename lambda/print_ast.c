
#include "transpiler.h"

void writeNodeSource(Transpiler* tp, TSNode node) {
    int start_byte = ts_node_start_byte(node);
    const char* start = tp->source + start_byte;
    strbuf_append_str_n(tp->code_buf, start, ts_node_end_byte(node) - start_byte);
}

void writeType(Transpiler* tp, LambdaType *type) {
    TypeId type_id = type->type_id;
    switch (type_id) {
    case LMD_TYPE_NULL:
        strbuf_append_str(tp->code_buf, "void*");
        break;
    case LMD_TYPE_ANY:
        strbuf_append_str(tp->code_buf, "Item");
        break;
    case LMD_TYPE_ERROR:
        strbuf_append_str(tp->code_buf, "Item");
        break;        
    case LMD_TYPE_BOOL:
        strbuf_append_str(tp->code_buf, "bool");
        break;        
    case LMD_TYPE_INT:
        strbuf_append_str(tp->code_buf, "int");
        break;
    case LMD_TYPE_FLOAT:
        strbuf_append_str(tp->code_buf, "float");
        break;
    case LMD_TYPE_DOUBLE:
        strbuf_append_str(tp->code_buf, "double");
        break;
    case LMD_TYPE_STRING:
        strbuf_append_str(tp->code_buf, "char*");
        break;
    case LMD_TYPE_ARRAY:
        LambdaTypeArray *array_type = (LambdaTypeArray*)type;
        if (array_type->nested && array_type->nested->type_id == LMD_TYPE_INT) {
            strbuf_append_str(tp->code_buf, "ArrayInt*");
        } else {
            strbuf_append_str(tp->code_buf, "Array*");
        }
        break;
    case LMD_TYPE_MAP:
        strbuf_append_str(tp->code_buf, "Map*");
        break;
    default:
        printf("unknown type %d\n", type_id);
    }
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

void print_label(int indent, const char *label) {
    for (int i = 0; i < indent; i++) { printf("  "); }
    printf("%s\n", label);
}

void print_ast_node(AstNode *node, int indent) {
    for (int i = 0; i < indent; i++) { printf("  "); }
    // get the function name
    switch(node->node_type) {
    case AST_NODE_IDENT:
        printf("[ident:%.*s:%s]\n", (int)((AstNamedNode*)node)->name.length, 
            ((AstNamedNode*)node)->name.str, formatType(node->type));
        break;
    case AST_NODE_PRIMARY:
        printf("[primary expr:%s]\n", formatType(node->type));
        if (((AstPrimaryNode*)node)->expr) {
            print_ast_node(((AstPrimaryNode*)node)->expr, indent + 1);
        }
        break;
    case AST_NODE_BINARY:
        printf("[binary expr:%s]\n", formatType(node->type));
        print_ast_node(((AstBinaryNode*)node)->left, indent + 1);
        print_ast_node(((AstBinaryNode*)node)->right, indent + 1);
        break;
    case AST_NODE_IF_EXPR:
        printf("[if expr:%s]\n", formatType(node->type));
        AstIfExprNode* if_node = (AstIfExprNode*)node;
        print_ast_node(if_node->cond, indent + 1);
        print_label(indent + 1, "then:");
        print_ast_node(if_node->then, indent + 1);
        if (if_node->otherwise) {
            print_label(indent + 1, "else:");            
            print_ast_node(if_node->otherwise, indent + 1);
        }
        break;
    case AST_NODE_LET_EXPR:  case AST_NODE_LET_STAM:
        printf("[let %s:%s]\n", node->node_type == AST_NODE_LET_EXPR ? "expr" : "stam", formatType(node->type));
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            print_label(indent + 1, "declare:");
            print_ast_node(declare, indent + 1);
            declare = declare->next;
        }
        if (node->node_type == AST_NODE_LET_EXPR) {
            print_label(indent + 1, "then:");
            print_ast_node(((AstLetNode*)node)->then, indent + 1);
        }
        break;
    case AST_NODE_FOR_EXPR:
        printf("[for %s:%s]\n", node->node_type == AST_NODE_FOR_EXPR ? "expr" : "stam", formatType(node->type));
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            print_label(indent + 1, "loop:");
            print_ast_node(loop, indent + 1);
            loop = loop->next;
        }
        if (node->node_type == AST_NODE_FOR_EXPR) {
            print_label(indent + 1, "then:");
            print_ast_node(((AstForNode*)node)->then, indent + 1);
        }
        break;
    case AST_NODE_ASSIGN:
        printf("[assign expr:%s]\n", formatType(node->type));
        print_ast_node(((AstNamedNode*)node)->as, indent + 1);
        break;
    case AST_NODE_LOOP:
        printf("[loop expr:%s]\n", formatType(node->type));
        print_ast_node(((AstNamedNode*)node)->as, indent + 1);
        break;
    case AST_NODE_ARRAY:
        printf("[array expr:%s]\n", formatType(node->type));
        AstNode *item = ((AstArrayNode*)node)->item;
        while (item) {
            print_label(indent + 1, "item:");
            print_ast_node(item, indent + 1);
            item = item->next;
        }        
        break;
    case AST_NODE_LIST:
        printf("[list expr:%s]\n", formatType(node->type));
        AstNode *li = ((AstArrayNode*)node)->item;
        while (li) {
            print_label(indent + 1, "item:");
            print_ast_node(li, indent + 1);
            li = li->next;
        }        
        break;        
    case AST_NODE_MAP:
        printf("[map expr:%s]\n", formatType(node->type));
        AstNamedNode *nm_item = ((AstMapNode*)node)->item;
        while (nm_item) {
            print_label(indent + 1, "item:");
            print_ast_node((AstNode*)nm_item, indent + 1);
            nm_item = (AstNamedNode*)nm_item->next;
        }
        break;
    case AST_NODE_PARAM:
        AstNamedNode* param = (AstNamedNode*)node;
        printf("[param: %.*s:%s]\n", (int)param->name.length, param->name.str, formatType(node->type));
        break;
    case AST_NODE_FIELD_EXPR:
        printf("[field expr:%s]\n", formatType(node->type));
        print_label(indent + 1, "object:");
        print_ast_node(((AstFieldNode*)node)->object, indent + 1);
        print_label(indent + 1, "field:");     
        print_ast_node(((AstFieldNode*)node)->field, indent + 1);
        break;
    case AST_NODE_CALL_EXPR:
        printf("[call expr:%s]\n", formatType(node->type));
        print_ast_node(((AstCallNode*)node)->function, indent + 1);
        print_label(indent + 1, "args:"); 
        AstNode* arg = ((AstCallNode*)node)->argument;
        while (arg) {
            print_ast_node(arg, indent + 1);
            arg = arg->next;
        }
        break;
    case AST_NODE_FUNC:
        AstFuncNode* func = (AstFuncNode*)node;
        printf("[function: %.*s:%s]\n", (int)func->name.length, func->name.str, formatType(node->type));
        print_label(indent + 1, "params:"); 
        AstNode* fn_param = (AstNode*)func->param;
        while (fn_param) {
            print_ast_node(fn_param, indent + 1);
            fn_param = fn_param->next;
        }
        print_ast_node(func->body, indent + 1);
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

