#ifndef NDEBUG
#include "py_transpiler.hpp"
#include "../../lib/log.h"
#include <cstdio>
#include <cstring>

static const char* py_node_type_name(PyAstNodeType type) {
    switch (type) {
        case PY_AST_NODE_MODULE: return "Module";
        case PY_AST_NODE_EXPRESSION_STATEMENT: return "ExprStmt";
        case PY_AST_NODE_ASSIGNMENT: return "Assign";
        case PY_AST_NODE_AUGMENTED_ASSIGNMENT: return "AugAssign";
        case PY_AST_NODE_RETURN: return "Return";
        case PY_AST_NODE_IF: return "If";
        case PY_AST_NODE_ELIF: return "Elif";
        case PY_AST_NODE_WHILE: return "While";
        case PY_AST_NODE_FOR: return "For";
        case PY_AST_NODE_BREAK: return "Break";
        case PY_AST_NODE_CONTINUE: return "Continue";
        case PY_AST_NODE_PASS: return "Pass";
        case PY_AST_NODE_FUNCTION_DEF: return "FuncDef";
        case PY_AST_NODE_CLASS_DEF: return "ClassDef";
        case PY_AST_NODE_IMPORT: return "Import";
        case PY_AST_NODE_IMPORT_FROM: return "ImportFrom";
        case PY_AST_NODE_GLOBAL: return "Global";
        case PY_AST_NODE_NONLOCAL: return "Nonlocal";
        case PY_AST_NODE_DEL: return "Del";
        case PY_AST_NODE_ASSERT: return "Assert";
        case PY_AST_NODE_RAISE: return "Raise";
        case PY_AST_NODE_TRY: return "Try";
        case PY_AST_NODE_EXCEPT: return "Except";
        case PY_AST_NODE_WITH: return "With";
        case PY_AST_NODE_BLOCK: return "Block";
        case PY_AST_NODE_IDENTIFIER: return "Id";
        case PY_AST_NODE_LITERAL: return "Literal";
        case PY_AST_NODE_FSTRING: return "FString";
        case PY_AST_NODE_BINARY_OP: return "BinOp";
        case PY_AST_NODE_UNARY_OP: return "UnaryOp";
        case PY_AST_NODE_BOOLEAN_OP: return "BoolOp";
        case PY_AST_NODE_COMPARE: return "Compare";
        case PY_AST_NODE_CALL: return "Call";
        case PY_AST_NODE_ATTRIBUTE: return "Attr";
        case PY_AST_NODE_SUBSCRIPT: return "Subscript";
        case PY_AST_NODE_SLICE: return "Slice";
        case PY_AST_NODE_STARRED: return "Starred";
        case PY_AST_NODE_LIST: return "List";
        case PY_AST_NODE_TUPLE: return "Tuple";
        case PY_AST_NODE_DICT: return "Dict";
        case PY_AST_NODE_SET: return "Set";
        case PY_AST_NODE_LIST_COMPREHENSION: return "ListComp";
        case PY_AST_NODE_DICT_COMPREHENSION: return "DictComp";
        case PY_AST_NODE_SET_COMPREHENSION: return "SetComp";
        case PY_AST_NODE_GENERATOR_EXPRESSION: return "GenExpr";
        case PY_AST_NODE_CONDITIONAL_EXPR: return "CondExpr";
        case PY_AST_NODE_LAMBDA: return "Lambda";
        case PY_AST_NODE_NOT: return "Not";
        case PY_AST_NODE_KEYWORD_ARGUMENT: return "Keyword";
        case PY_AST_NODE_PARAMETER: return "Param";
        case PY_AST_NODE_DEFAULT_PARAMETER: return "DefParam";
        case PY_AST_NODE_TYPED_PARAMETER: return "TypedParam";
        case PY_AST_NODE_DICT_SPLAT_PARAMETER: return "DictSplat";
        case PY_AST_NODE_LIST_SPLAT_PARAMETER: return "ListSplat";
        case PY_AST_NODE_PAIR: return "Pair";
        case PY_AST_NODE_DECORATOR: return "Decorator";
        default: return "Unknown";
    }
}

static const char* py_op_name(PyOperator op) {
    switch (op) {
        case PY_OP_ADD: return "+";
        case PY_OP_SUB: return "-";
        case PY_OP_MUL: return "*";
        case PY_OP_DIV: return "/";
        case PY_OP_FLOOR_DIV: return "//";
        case PY_OP_MOD: return "%";
        case PY_OP_POW: return "**";
        case PY_OP_MATMUL: return "@";
        case PY_OP_LSHIFT: return "<<";
        case PY_OP_RSHIFT: return ">>";
        case PY_OP_BIT_AND: return "&";
        case PY_OP_BIT_OR: return "|";
        case PY_OP_BIT_XOR: return "^";
        case PY_OP_BIT_NOT: return "~";
        case PY_OP_EQ: return "==";
        case PY_OP_NE: return "!=";
        case PY_OP_LT: return "<";
        case PY_OP_LE: return "<=";
        case PY_OP_GT: return ">";
        case PY_OP_GE: return ">=";
        case PY_OP_AND: return "and";
        case PY_OP_OR: return "or";
        case PY_OP_NOT: return "not";
        case PY_OP_IN: return "in";
        case PY_OP_NOT_IN: return "not in";
        case PY_OP_IS: return "is";
        case PY_OP_IS_NOT: return "is not";
        case PY_OP_NEGATE: return "-";
        case PY_OP_POSITIVE: return "+";
        case PY_OP_ADD_ASSIGN: return "+=";
        case PY_OP_SUB_ASSIGN: return "-=";
        case PY_OP_MUL_ASSIGN: return "*=";
        case PY_OP_DIV_ASSIGN: return "/=";
        case PY_OP_FLOOR_DIV_ASSIGN: return "//=";
        case PY_OP_MOD_ASSIGN: return "%=";
        case PY_OP_POW_ASSIGN: return "**=";
        case PY_OP_MATMUL_ASSIGN: return "@=";
        case PY_OP_LSHIFT_ASSIGN: return "<<=";
        case PY_OP_RSHIFT_ASSIGN: return ">>=";
        case PY_OP_BIT_AND_ASSIGN: return "&=";
        case PY_OP_BIT_OR_ASSIGN: return "|=";
        case PY_OP_BIT_XOR_ASSIGN: return "^=";
        default: return "?";
    }
}

static void indent(int level) {
    for (int i = 0; i < level; i++) {
        printf("  ");
    }
}

void print_py_ast_node(PyAstNode* node, int depth) {
    if (!node) return;

    indent(depth);
    printf("%s", py_node_type_name(node->node_type));

    switch (node->node_type) {
        case PY_AST_NODE_IDENTIFIER: {
            PyIdentifierNode* id = (PyIdentifierNode*)node;
            if (id->name) {
                printf(" '%.*s'", (int)id->name->len, id->name->chars);
            }
            printf("\n");
            break;
        }
        case PY_AST_NODE_LITERAL: {
            PyLiteralNode* lit = (PyLiteralNode*)node;
            switch (lit->literal_type) {
                case PY_LITERAL_INT:
                    printf(" %lld\n", (long long)lit->value.int_value);
                    break;
                case PY_LITERAL_FLOAT:
                    printf(" %g\n", lit->value.float_value);
                    break;
                case PY_LITERAL_STRING:
                    if (lit->value.string_value) {
                        printf(" \"%.*s\"\n", (int)lit->value.string_value->len, lit->value.string_value->chars);
                    } else {
                        printf(" \"\"\n");
                    }
                    break;
                case PY_LITERAL_BOOLEAN:
                    printf(" %s\n", lit->value.boolean_value ? "True" : "False");
                    break;
                case PY_LITERAL_NONE:
                    printf(" None\n");
                    break;
            }
            break;
        }
        case PY_AST_NODE_BINARY_OP: {
            PyBinaryNode* bin = (PyBinaryNode*)node;
            printf(" %s\n", py_op_name(bin->op));
            print_py_ast_node(bin->left, depth + 1);
            print_py_ast_node(bin->right, depth + 1);
            break;
        }
        case PY_AST_NODE_UNARY_OP:
        case PY_AST_NODE_NOT: {
            PyUnaryNode* un = (PyUnaryNode*)node;
            printf(" %s\n", py_op_name(un->op));
            print_py_ast_node(un->operand, depth + 1);
            break;
        }
        case PY_AST_NODE_BOOLEAN_OP: {
            PyBooleanNode* bop = (PyBooleanNode*)node;
            printf(" %s\n", py_op_name(bop->op));
            print_py_ast_node(bop->left, depth + 1);
            print_py_ast_node(bop->right, depth + 1);
            break;
        }
        case PY_AST_NODE_COMPARE: {
            PyCompareNode* cmp = (PyCompareNode*)node;
            printf(" (");
            for (int i = 0; i < cmp->op_count; i++) {
                if (i > 0) printf(", ");
                printf("%s", py_op_name(cmp->ops[i]));
            }
            printf(")\n");
            print_py_ast_node(cmp->left, depth + 1);
            for (int i = 0; i < cmp->op_count; i++) {
                print_py_ast_node(cmp->comparators[i], depth + 1);
            }
            break;
        }
        case PY_AST_NODE_CALL: {
            PyCallNode* call = (PyCallNode*)node;
            printf(" (%d args)\n", call->arg_count);
            print_py_ast_node(call->function, depth + 1);
            PyAstNode* arg = call->arguments;
            while (arg) {
                print_py_ast_node(arg, depth + 1);
                arg = arg->next;
            }
            break;
        }
        case PY_AST_NODE_ATTRIBUTE: {
            PyAttributeNode* attr = (PyAttributeNode*)node;
            if (attr->attribute) {
                printf(" .%.*s\n", (int)attr->attribute->len, attr->attribute->chars);
            } else {
                printf("\n");
            }
            print_py_ast_node(attr->object, depth + 1);
            break;
        }
        case PY_AST_NODE_SUBSCRIPT: {
            PySubscriptNode* sub = (PySubscriptNode*)node;
            printf("\n");
            print_py_ast_node(sub->object, depth + 1);
            print_py_ast_node(sub->index, depth + 1);
            break;
        }
        case PY_AST_NODE_ASSIGNMENT: {
            PyAssignmentNode* assign = (PyAssignmentNode*)node;
            printf("\n");
            indent(depth + 1);
            printf("target:\n");
            print_py_ast_node(assign->targets, depth + 2);
            indent(depth + 1);
            printf("value:\n");
            print_py_ast_node(assign->value, depth + 2);
            break;
        }
        case PY_AST_NODE_AUGMENTED_ASSIGNMENT: {
            PyAugAssignmentNode* aug = (PyAugAssignmentNode*)node;
            printf(" %s\n", py_op_name(aug->op));
            print_py_ast_node(aug->target, depth + 1);
            print_py_ast_node(aug->value, depth + 1);
            break;
        }
        case PY_AST_NODE_IF:
        case PY_AST_NODE_ELIF: {
            PyIfNode* if_stmt = (PyIfNode*)node;
            printf("\n");
            indent(depth + 1);
            printf("test:\n");
            print_py_ast_node(if_stmt->test, depth + 2);
            indent(depth + 1);
            printf("body:\n");
            print_py_ast_node(if_stmt->body, depth + 2);
            if (if_stmt->elif_clauses) {
                PyAstNode* elif = if_stmt->elif_clauses;
                while (elif) {
                    print_py_ast_node(elif, depth + 1);
                    elif = elif->next;
                }
            }
            if (if_stmt->else_body) {
                indent(depth + 1);
                printf("else:\n");
                print_py_ast_node(if_stmt->else_body, depth + 2);
            }
            break;
        }
        case PY_AST_NODE_WHILE: {
            PyWhileNode* wh = (PyWhileNode*)node;
            printf("\n");
            indent(depth + 1);
            printf("test:\n");
            print_py_ast_node(wh->test, depth + 2);
            indent(depth + 1);
            printf("body:\n");
            print_py_ast_node(wh->body, depth + 2);
            break;
        }
        case PY_AST_NODE_FOR: {
            PyForNode* f = (PyForNode*)node;
            printf("\n");
            indent(depth + 1);
            printf("target:\n");
            print_py_ast_node(f->target, depth + 2);
            indent(depth + 1);
            printf("iter:\n");
            print_py_ast_node(f->iter, depth + 2);
            indent(depth + 1);
            printf("body:\n");
            print_py_ast_node(f->body, depth + 2);
            break;
        }
        case PY_AST_NODE_FUNCTION_DEF: {
            PyFunctionDefNode* func = (PyFunctionDefNode*)node;
            if (func->name) {
                printf(" '%.*s'\n", (int)func->name->len, func->name->chars);
            } else {
                printf("\n");
            }
            if (func->params) {
                indent(depth + 1);
                printf("params:\n");
                PyAstNode* p = func->params;
                while (p) {
                    print_py_ast_node(p, depth + 2);
                    p = p->next;
                }
            }
            indent(depth + 1);
            printf("body:\n");
            print_py_ast_node(func->body, depth + 2);
            break;
        }
        case PY_AST_NODE_CLASS_DEF: {
            PyClassDefNode* cls = (PyClassDefNode*)node;
            if (cls->name) {
                printf(" '%.*s'\n", (int)cls->name->len, cls->name->chars);
            } else {
                printf("\n");
            }
            print_py_ast_node(cls->body, depth + 1);
            break;
        }
        case PY_AST_NODE_RETURN: {
            PyReturnNode* ret = (PyReturnNode*)node;
            printf("\n");
            if (ret->value) print_py_ast_node(ret->value, depth + 1);
            break;
        }
        case PY_AST_NODE_MODULE: {
            PyModuleNode* mod = (PyModuleNode*)node;
            printf("\n");
            PyAstNode* stmt = mod->body;
            while (stmt) {
                print_py_ast_node(stmt, depth + 1);
                stmt = stmt->next;
            }
            break;
        }
        case PY_AST_NODE_BLOCK: {
            PyBlockNode* blk = (PyBlockNode*)node;
            printf("\n");
            PyAstNode* stmt = blk->statements;
            while (stmt) {
                print_py_ast_node(stmt, depth + 1);
                stmt = stmt->next;
            }
            break;
        }
        case PY_AST_NODE_EXPRESSION_STATEMENT: {
            PyExpressionStatementNode* es = (PyExpressionStatementNode*)node;
            printf("\n");
            print_py_ast_node(es->expression, depth + 1);
            break;
        }
        case PY_AST_NODE_LIST:
        case PY_AST_NODE_TUPLE:
        case PY_AST_NODE_SET: {
            PySequenceNode* seq = (PySequenceNode*)node;
            printf(" [%d]\n", seq->length);
            PyAstNode* elem = seq->elements;
            while (elem) {
                print_py_ast_node(elem, depth + 1);
                elem = elem->next;
            }
            break;
        }
        case PY_AST_NODE_DICT: {
            PyDictNode* dict = (PyDictNode*)node;
            printf(" {%d}\n", dict->length);
            PyAstNode* pair = dict->pairs;
            while (pair) {
                print_py_ast_node(pair, depth + 1);
                pair = pair->next;
            }
            break;
        }
        case PY_AST_NODE_PAIR: {
            PyPairNode* pair = (PyPairNode*)node;
            printf("\n");
            print_py_ast_node(pair->key, depth + 1);
            print_py_ast_node(pair->value, depth + 1);
            break;
        }
        case PY_AST_NODE_PARAMETER:
        case PY_AST_NODE_DEFAULT_PARAMETER:
        case PY_AST_NODE_TYPED_PARAMETER:
        case PY_AST_NODE_LIST_SPLAT_PARAMETER:
        case PY_AST_NODE_DICT_SPLAT_PARAMETER: {
            PyParamNode* p = (PyParamNode*)node;
            if (p->name) {
                printf(" '%.*s'", (int)p->name->len, p->name->chars);
            }
            printf("\n");
            if (p->default_value) {
                indent(depth + 1);
                printf("default:\n");
                print_py_ast_node(p->default_value, depth + 2);
            }
            break;
        }
        default:
            printf("\n");
            break;
    }
}
#endif // NDEBUG
