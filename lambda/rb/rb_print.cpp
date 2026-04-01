// rb_print.cpp — Ruby AST printer for debugging
#include "rb_transpiler.hpp"
#include "../../lib/log.h"
#include <cstdio>
#include <cstring>

const char* rb_node_type_name(RbAstNodeType type) {
    switch (type) {
        case RB_AST_NODE_PROGRAM: return "Program";
        case RB_AST_NODE_EXPRESSION_STATEMENT: return "ExprStmt";
        case RB_AST_NODE_ASSIGNMENT: return "Assign";
        case RB_AST_NODE_OP_ASSIGNMENT: return "OpAssign";
        case RB_AST_NODE_RETURN: return "Return";
        case RB_AST_NODE_IF: return "If";
        case RB_AST_NODE_UNLESS: return "Unless";
        case RB_AST_NODE_WHILE: return "While";
        case RB_AST_NODE_UNTIL: return "Until";
        case RB_AST_NODE_FOR: return "For";
        case RB_AST_NODE_CASE: return "Case";
        case RB_AST_NODE_WHEN: return "When";
        case RB_AST_NODE_BREAK: return "Break";
        case RB_AST_NODE_NEXT: return "Next";
        case RB_AST_NODE_METHOD_DEF: return "MethodDef";
        case RB_AST_NODE_CLASS_DEF: return "ClassDef";
        case RB_AST_NODE_MODULE_DEF: return "ModuleDef";
        case RB_AST_NODE_BEGIN_RESCUE: return "BeginRescue";
        case RB_AST_NODE_RESCUE: return "Rescue";
        case RB_AST_NODE_ENSURE: return "Ensure";
        case RB_AST_NODE_RAISE: return "Raise";
        case RB_AST_NODE_YIELD: return "Yield";
        case RB_AST_NODE_BLOCK: return "Block";
        case RB_AST_NODE_IDENTIFIER: return "Id";
        case RB_AST_NODE_SELF: return "Self";
        case RB_AST_NODE_LITERAL: return "Literal";
        case RB_AST_NODE_STRING_INTERPOLATION: return "StrInterp";
        case RB_AST_NODE_BINARY_OP: return "BinOp";
        case RB_AST_NODE_UNARY_OP: return "UnaryOp";
        case RB_AST_NODE_BOOLEAN_OP: return "BoolOp";
        case RB_AST_NODE_COMPARISON: return "Compare";
        case RB_AST_NODE_CALL: return "Call";
        case RB_AST_NODE_ATTRIBUTE: return "Attr";
        case RB_AST_NODE_SUBSCRIPT: return "Subscript";
        case RB_AST_NODE_ARRAY: return "Array";
        case RB_AST_NODE_HASH: return "Hash";
        case RB_AST_NODE_RANGE: return "Range";
        case RB_AST_NODE_PROC_LAMBDA: return "ProcLambda";
        case RB_AST_NODE_BLOCK_PASS: return "BlockPass";
        case RB_AST_NODE_SPLAT: return "Splat";
        case RB_AST_NODE_DOUBLE_SPLAT: return "DoubleSplat";
        case RB_AST_NODE_PAIR: return "Pair";
        case RB_AST_NODE_PARAMETER: return "Param";
        case RB_AST_NODE_DEFAULT_PARAMETER: return "DefParam";
        case RB_AST_NODE_IVAR: return "Ivar";
        case RB_AST_NODE_CVAR: return "Cvar";
        case RB_AST_NODE_GVAR: return "Gvar";
        case RB_AST_NODE_CONST: return "Const";
        case RB_AST_NODE_TERNARY: return "Ternary";
        case RB_AST_NODE_SYMBOL: return "Symbol";
        default: return "Unknown";
    }
}

const char* rb_op_name(RbOperator op) {
    switch (op) {
        case RB_OP_ADD: return "+";
        case RB_OP_SUB: return "-";
        case RB_OP_MUL: return "*";
        case RB_OP_DIV: return "/";
        case RB_OP_MOD: return "%";
        case RB_OP_POW: return "**";
        case RB_OP_EQ: return "==";
        case RB_OP_NEQ: return "!=";
        case RB_OP_LT: return "<";
        case RB_OP_LE: return "<=";
        case RB_OP_GT: return ">";
        case RB_OP_GE: return ">=";
        case RB_OP_CMP: return "<=>";
        case RB_OP_CASE_EQ: return "===";
        case RB_OP_AND: return "&&";
        case RB_OP_OR: return "||";
        case RB_OP_NOT: return "!";
        case RB_OP_BIT_AND: return "&";
        case RB_OP_BIT_OR: return "|";
        case RB_OP_BIT_XOR: return "^";
        case RB_OP_BIT_NOT: return "~";
        case RB_OP_LSHIFT: return "<<";
        case RB_OP_RSHIFT: return ">>";
        case RB_OP_NEGATE: return "-";
        case RB_OP_POSITIVE: return "+";
        default: return "?";
    }
}

static void rb_indent(int level) {
    for (int i = 0; i < level; i++) {
        printf("  ");
    }
}

void print_rb_ast_node(RbAstNode* node, int depth) {
    if (!node) return;

    rb_indent(depth);
    printf("%s", rb_node_type_name(node->node_type));

    switch (node->node_type) {
        case RB_AST_NODE_PROGRAM: {
            RbProgramNode* prog = (RbProgramNode*)node;
            printf("\n");
            RbAstNode* stmt = prog->body;
            while (stmt) {
                print_rb_ast_node(stmt, depth + 1);
                stmt = stmt->next;
            }
            break;
        }
        case RB_AST_NODE_IDENTIFIER: {
            RbIdentifierNode* id = (RbIdentifierNode*)node;
            if (id->name) {
                printf(" '%.*s'", (int)id->name->len, id->name->chars);
            }
            printf("\n");
            break;
        }
        case RB_AST_NODE_IVAR: {
            RbIvarNode* iv = (RbIvarNode*)node;
            if (iv->name) {
                printf(" @%.*s", (int)iv->name->len, iv->name->chars);
            }
            printf("\n");
            break;
        }
        case RB_AST_NODE_CVAR: {
            RbCvarNode* cv = (RbCvarNode*)node;
            if (cv->name) {
                printf(" @@%.*s", (int)cv->name->len, cv->name->chars);
            }
            printf("\n");
            break;
        }
        case RB_AST_NODE_GVAR: {
            RbGvarNode* gv = (RbGvarNode*)node;
            if (gv->name) {
                printf(" $%.*s", (int)gv->name->len, gv->name->chars);
            }
            printf("\n");
            break;
        }
        case RB_AST_NODE_CONST: {
            RbConstNode* cn = (RbConstNode*)node;
            if (cn->name) {
                printf(" %.*s", (int)cn->name->len, cn->name->chars);
            }
            printf("\n");
            break;
        }
        case RB_AST_NODE_SYMBOL: {
            RbSymbolNode* sym = (RbSymbolNode*)node;
            if (sym->name) {
                printf(" :%.*s", (int)sym->name->len, sym->name->chars);
            }
            printf("\n");
            break;
        }
        case RB_AST_NODE_SELF: {
            printf("\n");
            break;
        }
        case RB_AST_NODE_LITERAL: {
            RbLiteralNode* lit = (RbLiteralNode*)node;
            switch (lit->literal_type) {
                case RB_LITERAL_INT:
                    printf(" %lld\n", (long long)lit->value.int_value);
                    break;
                case RB_LITERAL_FLOAT:
                    printf(" %g\n", lit->value.float_value);
                    break;
                case RB_LITERAL_STRING:
                    if (lit->value.string_value) {
                        printf(" \"%.*s\"\n", (int)lit->value.string_value->len, lit->value.string_value->chars);
                    } else {
                        printf(" \"\"\n");
                    }
                    break;
                case RB_LITERAL_SYMBOL:
                    if (lit->value.string_value) {
                        printf(" :%.*s\n", (int)lit->value.string_value->len, lit->value.string_value->chars);
                    } else {
                        printf(" :\n");
                    }
                    break;
                case RB_LITERAL_BOOLEAN:
                    printf(" %s\n", lit->value.boolean_value ? "true" : "false");
                    break;
                case RB_LITERAL_NIL:
                    printf(" nil\n");
                    break;
            }
            break;
        }
        case RB_AST_NODE_BINARY_OP: {
            RbBinaryNode* bin = (RbBinaryNode*)node;
            printf(" %s\n", rb_op_name(bin->op));
            print_rb_ast_node(bin->left, depth + 1);
            print_rb_ast_node(bin->right, depth + 1);
            break;
        }
        case RB_AST_NODE_UNARY_OP: {
            RbUnaryNode* un = (RbUnaryNode*)node;
            printf(" %s\n", rb_op_name(un->op));
            print_rb_ast_node(un->operand, depth + 1);
            break;
        }
        case RB_AST_NODE_BOOLEAN_OP: {
            RbBooleanNode* bop = (RbBooleanNode*)node;
            printf(" %s\n", rb_op_name(bop->op));
            print_rb_ast_node(bop->left, depth + 1);
            if (bop->right) print_rb_ast_node(bop->right, depth + 1);
            break;
        }
        case RB_AST_NODE_COMPARISON: {
            RbBinaryNode* cmp = (RbBinaryNode*)node;
            printf(" %s\n", rb_op_name(cmp->op));
            print_rb_ast_node(cmp->left, depth + 1);
            print_rb_ast_node(cmp->right, depth + 1);
            break;
        }
        case RB_AST_NODE_CALL: {
            RbCallNode* call = (RbCallNode*)node;
            if (call->method_name) {
                printf(" .%.*s (%d args)", (int)call->method_name->len, call->method_name->chars, call->arg_count);
            } else {
                printf(" (%d args)", call->arg_count);
            }
            printf("\n");
            if (call->receiver) {
                rb_indent(depth + 1);
                printf("recv:\n");
                print_rb_ast_node(call->receiver, depth + 2);
            }
            RbAstNode* arg = call->args;
            while (arg) {
                print_rb_ast_node(arg, depth + 1);
                arg = arg->next;
            }
            if (call->block) {
                rb_indent(depth + 1);
                printf("block:\n");
                print_rb_ast_node(call->block, depth + 2);
            }
            break;
        }
        case RB_AST_NODE_ATTRIBUTE: {
            RbAttributeNode* attr = (RbAttributeNode*)node;
            if (attr->attr_name) {
                printf(" .%.*s\n", (int)attr->attr_name->len, attr->attr_name->chars);
            } else {
                printf("\n");
            }
            print_rb_ast_node(attr->object, depth + 1);
            break;
        }
        case RB_AST_NODE_SUBSCRIPT: {
            RbSubscriptNode* sub = (RbSubscriptNode*)node;
            printf("\n");
            print_rb_ast_node(sub->object, depth + 1);
            print_rb_ast_node(sub->index, depth + 1);
            break;
        }
        case RB_AST_NODE_ASSIGNMENT: {
            RbAssignmentNode* assign = (RbAssignmentNode*)node;
            printf("\n");
            rb_indent(depth + 1);
            printf("target:\n");
            print_rb_ast_node(assign->target, depth + 2);
            rb_indent(depth + 1);
            printf("value:\n");
            print_rb_ast_node(assign->value, depth + 2);
            break;
        }
        case RB_AST_NODE_OP_ASSIGNMENT: {
            RbOpAssignmentNode* opa = (RbOpAssignmentNode*)node;
            printf(" %s=\n", rb_op_name(opa->op));
            print_rb_ast_node(opa->target, depth + 1);
            print_rb_ast_node(opa->value, depth + 1);
            break;
        }
        case RB_AST_NODE_IF:
        case RB_AST_NODE_UNLESS: {
            RbIfNode* if_stmt = (RbIfNode*)node;
            if (if_stmt->is_modifier) printf(" (modifier)");
            printf("\n");
            rb_indent(depth + 1);
            printf("cond:\n");
            print_rb_ast_node(if_stmt->condition, depth + 2);
            rb_indent(depth + 1);
            printf("then:\n");
            print_rb_ast_node(if_stmt->then_body, depth + 2);
            if (if_stmt->elsif_chain) {
                RbAstNode* elsif = if_stmt->elsif_chain;
                while (elsif) {
                    print_rb_ast_node(elsif, depth + 1);
                    elsif = elsif->next;
                }
            }
            if (if_stmt->else_body) {
                rb_indent(depth + 1);
                printf("else:\n");
                print_rb_ast_node(if_stmt->else_body, depth + 2);
            }
            break;
        }
        case RB_AST_NODE_WHILE:
        case RB_AST_NODE_UNTIL: {
            RbWhileNode* wh = (RbWhileNode*)node;
            printf("\n");
            rb_indent(depth + 1);
            printf("cond:\n");
            print_rb_ast_node(wh->condition, depth + 2);
            rb_indent(depth + 1);
            printf("body:\n");
            print_rb_ast_node(wh->body, depth + 2);
            break;
        }
        case RB_AST_NODE_FOR: {
            RbForNode* f = (RbForNode*)node;
            printf("\n");
            rb_indent(depth + 1);
            printf("var:\n");
            print_rb_ast_node(f->variable, depth + 2);
            rb_indent(depth + 1);
            printf("in:\n");
            print_rb_ast_node(f->collection, depth + 2);
            rb_indent(depth + 1);
            printf("body:\n");
            print_rb_ast_node(f->body, depth + 2);
            break;
        }
        case RB_AST_NODE_CASE: {
            RbCaseNode* cs = (RbCaseNode*)node;
            printf("\n");
            if (cs->subject) {
                rb_indent(depth + 1);
                printf("subject:\n");
                print_rb_ast_node(cs->subject, depth + 2);
            }
            RbAstNode* w = cs->whens;
            while (w) {
                print_rb_ast_node(w, depth + 1);
                w = w->next;
            }
            if (cs->else_body) {
                rb_indent(depth + 1);
                printf("else:\n");
                print_rb_ast_node(cs->else_body, depth + 2);
            }
            break;
        }
        case RB_AST_NODE_WHEN: {
            RbWhenNode* wn = (RbWhenNode*)node;
            printf("\n");
            rb_indent(depth + 1);
            printf("patterns:\n");
            RbAstNode* pat = wn->patterns;
            while (pat) {
                print_rb_ast_node(pat, depth + 2);
                pat = pat->next;
            }
            rb_indent(depth + 1);
            printf("body:\n");
            print_rb_ast_node(wn->body, depth + 2);
            break;
        }
        case RB_AST_NODE_METHOD_DEF: {
            RbMethodDefNode* meth = (RbMethodDefNode*)node;
            if (meth->is_class_method) printf(" self.");
            if (meth->name) {
                printf(" '%.*s'", (int)meth->name->len, meth->name->chars);
            }
            printf(" (%d params)\n", meth->param_count);
            if (meth->params) {
                rb_indent(depth + 1);
                printf("params:\n");
                RbAstNode* p = meth->params;
                while (p) {
                    print_rb_ast_node(p, depth + 2);
                    p = p->next;
                }
            }
            rb_indent(depth + 1);
            printf("body:\n");
            print_rb_ast_node(meth->body, depth + 2);
            break;
        }
        case RB_AST_NODE_CLASS_DEF: {
            RbClassDefNode* cls = (RbClassDefNode*)node;
            if (cls->name) {
                printf(" '%.*s'", (int)cls->name->len, cls->name->chars);
            }
            printf("\n");
            if (cls->superclass) {
                rb_indent(depth + 1);
                printf("< ");
                print_rb_ast_node(cls->superclass, 0);
            }
            print_rb_ast_node(cls->body, depth + 1);
            break;
        }
        case RB_AST_NODE_MODULE_DEF: {
            RbModuleDefNode* mod = (RbModuleDefNode*)node;
            if (mod->name) {
                printf(" '%.*s'\n", (int)mod->name->len, mod->name->chars);
            } else {
                printf("\n");
            }
            print_rb_ast_node(mod->body, depth + 1);
            break;
        }
        case RB_AST_NODE_RETURN: {
            RbReturnNode* ret = (RbReturnNode*)node;
            printf("\n");
            if (ret->value) print_rb_ast_node(ret->value, depth + 1);
            break;
        }
        case RB_AST_NODE_YIELD: {
            RbYieldNode* y = (RbYieldNode*)node;
            printf(" (%d args)\n", y->arg_count);
            RbAstNode* arg = y->args;
            while (arg) {
                print_rb_ast_node(arg, depth + 1);
                arg = arg->next;
            }
            break;
        }
        case RB_AST_NODE_BLOCK: {
            RbBlockNode* blk = (RbBlockNode*)node;
            printf(" (%d params)\n", blk->param_count);
            if (blk->params) {
                rb_indent(depth + 1);
                printf("params:\n");
                RbAstNode* p = blk->params;
                while (p) {
                    print_rb_ast_node(p, depth + 2);
                    p = p->next;
                }
            }
            rb_indent(depth + 1);
            printf("body:\n");
            print_rb_ast_node(blk->body, depth + 2);
            break;
        }
        case RB_AST_NODE_ARRAY: {
            RbArrayNode* arr = (RbArrayNode*)node;
            printf(" [%d]\n", arr->count);
            RbAstNode* elem = arr->elements;
            while (elem) {
                print_rb_ast_node(elem, depth + 1);
                elem = elem->next;
            }
            break;
        }
        case RB_AST_NODE_HASH: {
            RbHashNode* hash = (RbHashNode*)node;
            printf(" {%d}\n", hash->count);
            RbAstNode* pair = hash->pairs;
            while (pair) {
                print_rb_ast_node(pair, depth + 1);
                pair = pair->next;
            }
            break;
        }
        case RB_AST_NODE_PAIR: {
            RbPairNode* pair = (RbPairNode*)node;
            printf("\n");
            print_rb_ast_node(pair->key, depth + 1);
            print_rb_ast_node(pair->value, depth + 1);
            break;
        }
        case RB_AST_NODE_RANGE: {
            RbRangeNode* rng = (RbRangeNode*)node;
            printf(" %s\n", rng->exclusive ? "..." : "..");
            print_rb_ast_node(rng->start, depth + 1);
            print_rb_ast_node(rng->end, depth + 1);
            break;
        }
        case RB_AST_NODE_TERNARY: {
            RbTernaryNode* tern = (RbTernaryNode*)node;
            printf("\n");
            rb_indent(depth + 1);
            printf("cond:\n");
            print_rb_ast_node(tern->condition, depth + 2);
            rb_indent(depth + 1);
            printf("true:\n");
            print_rb_ast_node(tern->true_expr, depth + 2);
            rb_indent(depth + 1);
            printf("false:\n");
            print_rb_ast_node(tern->false_expr, depth + 2);
            break;
        }
        case RB_AST_NODE_STRING_INTERPOLATION: {
            RbStringInterpNode* si = (RbStringInterpNode*)node;
            printf(" (%d parts)\n", si->part_count);
            RbAstNode* part = si->parts;
            while (part) {
                print_rb_ast_node(part, depth + 1);
                part = part->next;
            }
            break;
        }
        case RB_AST_NODE_PARAMETER:
        case RB_AST_NODE_DEFAULT_PARAMETER: {
            RbParamNode* p = (RbParamNode*)node;
            if (p->is_block) printf(" &");
            else if (p->is_double_splat) printf(" **");
            else if (p->is_splat) printf(" *");
            if (p->name) {
                printf("'%.*s'", (int)p->name->len, p->name->chars);
            }
            printf("\n");
            if (p->default_value) {
                rb_indent(depth + 1);
                printf("default:\n");
                print_rb_ast_node(p->default_value, depth + 2);
            }
            break;
        }
        case RB_AST_NODE_BEGIN_RESCUE: {
            RbBeginRescueNode* br = (RbBeginRescueNode*)node;
            printf("\n");
            rb_indent(depth + 1);
            printf("body:\n");
            print_rb_ast_node(br->body, depth + 2);
            RbAstNode* r = br->rescues;
            while (r) {
                print_rb_ast_node(r, depth + 1);
                r = r->next;
            }
            if (br->else_body) {
                rb_indent(depth + 1);
                printf("else:\n");
                print_rb_ast_node(br->else_body, depth + 2);
            }
            if (br->ensure_body) {
                rb_indent(depth + 1);
                printf("ensure:\n");
                print_rb_ast_node(br->ensure_body, depth + 2);
            }
            break;
        }
        case RB_AST_NODE_RESCUE: {
            RbRescueNode* resc = (RbRescueNode*)node;
            if (resc->variable_name) {
                printf(" => %.*s", (int)resc->variable_name->len, resc->variable_name->chars);
            }
            printf("\n");
            if (resc->exception_classes) {
                rb_indent(depth + 1);
                printf("types:\n");
                RbAstNode* ec = resc->exception_classes;
                while (ec) {
                    print_rb_ast_node(ec, depth + 2);
                    ec = ec->next;
                }
            }
            rb_indent(depth + 1);
            printf("body:\n");
            print_rb_ast_node(resc->body, depth + 2);
            break;
        }
        case RB_AST_NODE_SPLAT: {
            RbSplatNode* sp = (RbSplatNode*)node;
            printf("\n");
            print_rb_ast_node(sp->operand, depth + 1);
            break;
        }
        case RB_AST_NODE_BLOCK_PASS: {
            RbBlockPassNode* bp = (RbBlockPassNode*)node;
            printf("\n");
            print_rb_ast_node(bp->value, depth + 1);
            break;
        }
        case RB_AST_NODE_EXPRESSION_STATEMENT: {
            printf("\n");
            // expression_statement wraps a single expression
            // the expression is stored as the next node after the base
            // use the pattern from rb_ast: it just contains an expression child
            // But we don't have an ExpressionStatementNode struct, we use
            // the base node's next-sibling approach. Actually, for expression
            // statements we need to handle them specially.
            break;
        }
        default:
            printf("\n");
            break;
    }
}
